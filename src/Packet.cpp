#include<Packet.h>

uint16_t                            Packet::_nextId=0;
ASMQ_PACKET_MAP                     Packet::_inbound;
ASMQ_PACKET_MAP                     Packet::_outbound;
std::vector<Packet*>                     Packet::_garbage;
//
Packet::~Packet(){
    ASMQ_BLOCK_Q cq=_blox;
    while(!cq.empty()){
        free(cq.front().second);
        cq.pop();
    }
}

void Packet::_ACK(ASMQ_PACKET_MAP* m,uint16_t id){
    if(m->count(id)){
        Packet* p=(*m)[id];
        delete p;
        m->erase(id);
    } //else ASMQ_PRINT("WHO TF IS %d???\n",id);
}

uint8_t* Packet::_block(size_t size){
    _bs+=size+2;
    _blox.push(std::make_pair(size,static_cast<uint8_t*>(malloc(size))));
    return _blox.back().second;
}

void Packet::_build(bool hold){
    _begin();
    uint32_t sx=_bs+_hdrAdjust;
    if(_hasId) sx+=2;
    std::vector<uint8_t> rl=_rl(sx);
    sx+=1+rl.size();
    uint8_t* base=(uint8_t*) malloc(sx);
    uint8_t* snd_buf=base;
    *snd_buf++=_controlcode;
    for(auto const& r:rl) *snd_buf++=r;
    if(_hasId) snd_buf=_poke16(snd_buf,_id);
    snd_buf=_middle(snd_buf);
    while(!_blox.empty()){
        uint16_t n=_blox.front().first;
        uint8_t* p=_blox.front().second;
        snd_buf=_poke16(snd_buf,n);
        memcpy(snd_buf,p,n);
        snd_buf+=n;
        free(p);
        _blox.pop();
    }
    _end(snd_buf);
    _blox.push(std::make_pair(sx,base));
    if(!hold) _release(base,sx);
}

void Packet::_clearMap(ASMQ_PACKET_MAP* m,std::function<bool(PublishPacket*)> pred){
    std::vector<uint16_t> morituri;
    for(auto const& p:*m){
        PublishPacket*  pub=reinterpret_cast<PublishPacket*>(p.second);
        if(pred(pub)) morituri.push_back(pub->_id);
    }
    for(auto const& i:morituri) _ACK(m,i);
}

void Packet::_clearPacketMap(std::function<bool(PublishPacket*)> pred){
    _clearMap(&_outbound,pred);
    _clearMap(&_inbound,pred);
}

std::pair<uint32_t,uint8_t> Packet::_getrl(uint8_t* p){
    uint32_t multiplier = 1;
    uint32_t value = 0;
    uint8_t encodedByte,len=0;
    do{
        encodedByte = *p++;
        len++;
        value += (encodedByte & 127) * multiplier;
        multiplier *= 128;
//                        if (multiplier > 128*128*128) ASMQ_PRINT("throw Error(Malformed Remaining Length)\n");
    } while ((encodedByte & 128) != 0);
    return std::make_pair(value,len);
}

void Packet::_idGarbage(uint16_t id){
    _initGC();
    uint8_t base[4]={_controlcode,0x2};
    _poke16(&base[2],id);
    _release(&base[0],4);
}

void Packet::_initId(){
    _id=++_nextId;
    _outbound[_id]=this;
}


uint8_t* Packet::_mem(const void* v,size_t size){
    if(size){
        _bs+=size+2;
        uint8_t* p=static_cast<uint8_t*>(malloc(size));
        _blox.push(std::make_pair(size,p));
        memcpy(p,v,size);
        return p;
    }
    return nullptr;
}

uint8_t* Packet::_poke16(uint8_t* p,uint16_t u){
    *p++=(u & 0xff00) >> 8;
    *p++=u & 0xff;
    return p;
}

void Packet::_release(uint8_t* base,size_t len){
    uint8_t type=base[0];
//                    ASMQ_PRINT("---> %02x %08x sz=%d\n",type,base,len);
    _caller->add((const char*) base,len);
    _caller->send();
}

std::vector<uint8_t> Packet::_rl(uint32_t X){
    std::vector<uint8_t> rl;
    uint8_t encodedByte;
    do{
        encodedByte = X % 128;
        X = X / 128;
        if ( X > 0 ) encodedByte = encodedByte | 128;
        rl.push_back(encodedByte);
    } while ( X> 0 );
    return rl;
}

void Packet::_shortGarbage(){
    _initGC();
    uint8_t base[2]={_controlcode,0x0};
    _release(&base[0],2);
}
//
ConnectPacket::ConnectPacket(): Packet(CONNECT,10){
    _initGC();
    _begin=[this]{
        if(AsyncMQTT::_cleanSession) protocol[7]|=CLEAN_SESSION;
        if(AsyncMQTT::_willRetain) protocol[7]|=WILL_RETAIN;
        if(AsyncMQTT::_willQos) protocol[7]|=(AsyncMQTT::_willQos==1) ? WILL_QOS1:WILL_QOS2;
        uint8_t* pClientId=_stringblock(AsyncMQTT::_clientId);
        if(AsyncMQTT::_willTopic.size()){
            _stringblock(AsyncMQTT::_willTopic);
            _stringblock(AsyncMQTT::_willPayload);
            protocol[7]|=WILL;
        }
        if(AsyncMQTT::_username.size()){
            _stringblock(AsyncMQTT::_username);
            protocol[7]|=USERNAME;
        }
        if(AsyncMQTT::_password.size()){
            _stringblock(AsyncMQTT::_password);
            protocol[7]|=PASSWORD;
        }
    };
    _middle=[this](uint8_t* p){
        memcpy(p,&protocol,8);p+=8;
        return _poke16(p,AsyncMQTT::_keepalive);
    };
    _build();
}
PublishPacket::PublishPacket(const char* topic, uint8_t qos, bool retain, uint8_t* payload, size_t length, bool dup,uint16_t givenId):
    _topic(topic),_qos(qos),_retain(retain),_length(length),_dup(dup),_givenId(givenId),Packet(PUBLISH) {
        _retries=AsyncMQTT::_maxRetries;
        _begin=[this]{ 
            _stringblock(CSTR(_topic));
            _bs+=_length;
            byte flags=_retain;
            flags|=(_dup << 3);
            if(_qos) {
                if(_givenId){
                    _id=_givenId;
                    _inbound[_id]=this;
                } 
                else _initId();
                flags|=(_qos << 1);
                _bs+=2; // because Packet id will be added
            } else _initGC();
            _controlcode|=flags;
        };
        _end=[this,payload](uint8_t* p){
            uint8_t* p2=_qos ? _poke16(p,_id):p;
            memcpy(p2,payload,_length);
        };
        _build(_givenId);
}
bool PublishPacket::resend(){
    if(_givenId) return _qos==1; // INBOUND
    else { // outbound
        if(_retries--){
            if(_qos==1){
                _blox.front().second[0]|=0x08; // set dup
                _release(_blox.front().second,_blox.front().first);
            }
            else new PubrelPacket(_id);
            return false; // stay of execution
        } else return true;
    }
}

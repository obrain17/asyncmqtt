#include<Packet.h>

uint16_t                            Packet::_nextId=0;
ASMQ_PACKET_MAP                     Packet::_inbound;
ASMQ_PACKET_MAP                     Packet::_outbound;
std::queue<ADFP>                    Packet::_flowControl;
ADFP                                Packet::_unAcked=nullptr;
uint16_t                            Packet::_uaId=0;;
bool                                Packet::_pcb_busy=false;

Packet::~Packet(){
//    if(!_blox.empty()) ASMQ_PRINT("FATAL PACKET %02x id=%d still has %d blox!!!\n",_controlcode,_id,_blox.size());
//  ASMQ_PRINT("PACKET %02x id=%d DIES: its data @ %08x hangs on!\n",_controlcode,_id);
}

void Packet::_ackTCP(size_t len, uint32_t time){
    _pcb_busy = false;

    if(_unAcked){
        ASMQ_PRINT("TCP ACK len=%d time=%d ua=%08x typ=%02x uaId=%d\n",time,len,_unAcked,_unAcked[0],_uaId);
        if(((_unAcked[0] & 0xf0) == 0x30) && _uaId ){
            ASMQ_PRINT("QOS PUBLISH - DO NOT KILL!\n"); // the ptr is left hanging but lives in either _inbound or _outbound
        }
        else {
//            ASMQ_PRINT("GARBAGE: KILL %08x\n",_unAcked);
            free(_unAcked);
            _unAcked=nullptr;
            _uaId=0;
        }
    } else ASMQ_PRINT("FATAL!: ACK WITH NO OUTBOUND !\n");

    if(!_flowControl.empty()){
//        AsyncMQTT::dumphex(_flowControl.front(),14);
        uint8_t* p=_flowControl.front();
        _flowControl.pop();
//        ASMQ_PRINT("UNQUEUE FRONT=%08x %02x @p[1]=%02x\n",p,p[0],p[1]);
        uint32_t length=_packetLength(p);
//        ASMQ_PRINT("UNQUEUE %08x %02x LEN=%d\n",p,p[0],length);
        _release(p,length);
    }
}

void Packet::_ACK(ASMQ_PACKET_MAP* m,uint16_t id){
    if(m->count(id)){
        ADFP p=(*m)[id];
        free(p); // THIS is where the memory leaks get mopped up!
        m->erase(id);
    } else ASMQ_PRINT("WHO TF IS %d???\n",id);
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
    ADFP base=(uint8_t*) malloc(sx); // Not a memleak - will be free'd when TCP ACKs it.
    ADFP snd_buf=base;
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
    _end(snd_buf,base);
    // the pointer in base MUST NOT BE FREED YET!
    // The class instance will go out of scope, its temp blocks are freed, but its built data MUST live on until ACK
    if(!hold) _release(base,sx);
}

void Packet::_clearFlowControlQ(){
    while(!_flowControl.empty()){
        ASMQ_PRINT("FQ: %08x %02x freed\n",_flowControl.front(),_flowControl.front()[0]);
        free(_flowControl.front());
        _flowControl.pop();
    }

    _unAcked=nullptr;
    _uaId=0;
}
void Packet::_clearMap(ASMQ_PACKET_MAP* m,ASMQ_RESEND_PRED pred){
//    AsyncMQTT::dump();
    std::vector<uint16_t> morituri;
    for(auto const& p:*m) if(pred(p.second)) morituri.push_back(p.first);
    for(auto const& i:morituri) _ACK(m,i);
}

void Packet::_clearPacketMap(ASMQ_RESEND_PRED ipred,ASMQ_RESEND_PRED opred){
    _clearMap(&_inbound,ipred);
    _clearMap(&_outbound,opred);
}
/*
struct ASMQ_DECODED_PUB {
    uint16_t        id;
    uint8_t         qos;
    bool            dup;
    bool            retain;
    std::string     topic;
    uint8_t*        payload;
    uint32_t        plen;
};

*/
ADP_t   Packet::_decodePub(uint8_t* data,uint8_t offset,uint32_t length){
    ADP_t       dp;
//    ASMQ_PRINT("DECODE %08x %02x off=%d len=%d\n",data,data[0],offset, length);
    //AsyncMQTT::dumphex(data,length);
    uint8_t     bits=data[0] & 0x0f;
    dp.dup=(bits & 0x8) >> 3;
    dp.qos=(bits & 0x6) >> 1;
    dp.retain=bits & 0x1;

    uint8_t* p=data+1+offset;
    dp.id=0;
    size_t tlen=AsyncMQTT::_peek16(p);p+=2;
    char c_topic[tlen+1];
    memcpy(&c_topic[0],p,tlen);c_topic[tlen]='\0';
    dp.topic.assign(&c_topic[0],tlen+1);
    p+=tlen;
    if(dp.qos) {
        dp.id=AsyncMQTT::_peek16(p);
        p+=2;
    }
    dp.plen=data+length-p;
    dp.payload=p;
//    ASMQ_PRINT("DECODE %s id=%d p=%08x plen=%d q=%d r=%d d=%d\n",CSTR(dp.topic),dp.id,p,dp.plen,dp.qos,dp.retain,dp.dup);
    return dp;
}

ASMQ_REM_LENGTH Packet::_getrl(uint8_t* p){
    uint32_t multiplier = 1;
    uint32_t value = 0;
    uint8_t encodedByte,len=0;
    do{
        encodedByte = *p++;
        len++;
        value += (encodedByte & 127) * multiplier;
        multiplier *= 128;
    } while ((encodedByte & 128) != 0);
    return std::make_pair(value,len);
}

void Packet::_idGarbage(uint16_t id){
    ADFP p=static_cast<uint8_t*>(malloc(4));
    p[0]=_controlcode;
    p[1]=_controlcode=2;
    _poke16(&p[2],id);
    _release(p,4);
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

uint32_t Packet::_packetLength(ADFP p){
    ASMQ_REM_LENGTH grl=_getrl(&p[1]); // tidy into this
//    ASMQ_PRINT("PL @ %08x RL=%d LPL=%d PL=%d\n",p,grl.first,grl.second,grl.first+1+grl.second);
    return grl.first+1+grl.second;
}

uint8_t* Packet::_poke16(uint8_t* p,uint16_t u){
    *p++=(u & 0xff00) >> 8;
    *p++=u & 0xff;
    return p;
}

bool Packet::_predInbound(ADFP p){
    ASMQ_PRINT("INBOUND RESEND? %08x %02x\n",p,p[0]);
    ASMQ_REM_LENGTH grl=Packet::_getrl(&p[1]); // tidy into this
    ASMQ_PRINT("INBOUND RESEND? PL=%d LPL=%d \n",grl.first,grl.second);
    ADP_t dp=_decodePub(p,grl.second,grl.first);
    ASMQ_PRINT("INBOUND RESEND? id=%d qos=%d\n",dp.id,dp.qos);
    return dp.qos==1;
}

bool Packet::_predOutbound(ADFP p){
    ASMQ_PRINT("OUTBOUND RESEND? %08x %02x\n",p,p[0]);
    ASMQ_REM_LENGTH grl=Packet::_getrl(&p[1]); // tidy into this
    ADP_t dp=_decodePub(p,grl.second,grl.first);
    ASMQ_PRINT("OUTBOUND RESEND? id=%d qos=%d\n",dp.id,dp.qos);
    if(dp.qos==1){
        p[0]|=0x08; // set dup
        _release(p,_packetLength(p));
    }
    else  PubrelPacket prp(dp.id);
    return false;
}

void Packet::_release(uint8_t* base,size_t len){
//    AsyncMQTT::dumphex(base,len);

    if ((!_pcb_busy) && (_caller->canSend())){
//        ASMQ_PRINT("_SEND %08x %02x\n",(void*)base,base[0]);
        _caller->add((const char*) base,len); // ESPAsyncTCP is WRONG on this, it should be a uint8_t*
        _caller->send();
        _pcb_busy = true;
        _unAcked=base; // save addres of "floating" unfreed packet so _ACK can use it
    }
    else _flowControl.push(base);
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
    uint8_t* p=static_cast<uint8_t*>(malloc(2));
    p[0]=_controlcode;
    p[1]=_controlcode=0;
    _release(p,2);
}

ConnectPacket::ConnectPacket(): Packet(CONNECT,10){
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
        return _poke16(p,AsyncMQTT::_keepalive / ASMQ_POLL_RATE);
    };

    _build();
}

PublishPacket::PublishPacket(const char* topic, uint8_t qos, bool retain, uint8_t* payload, size_t length, bool dup,uint16_t givenId):
    Packet(PUBLISH),_topic(topic),_qos(qos),_retain(retain),_length(length),_dup(dup),_givenId(givenId) {
        _retries=AsyncMQTT::_maxRetries;
        _begin=[this]{
            _stringblock(CSTR(_topic));
            _bs+=_length;
            byte flags=_retain;
            flags|=(_dup << 3);
        //
            if(_qos) {
                _id=_givenId || ++_nextId;
                flags|=(_qos << 1);
                _bs+=2; // because Packet id will be added
            }
            _controlcode|=flags;
        };
        _end=[this,payload](uint8_t* p,ADFP base){
            uint8_t* p2=_qos ? _poke16(p,_id):p;
            memcpy(p2,payload,_length);
            if(_givenId) _inbound[_givenId]=base;
            else if(_qos) _outbound[_uaId=_id]=base;
        };
        _build(_givenId);
}

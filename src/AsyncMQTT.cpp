#include <AsyncMQTT.h>
#include <Packet.h>

AsyncClient*             _caller;

string          AsyncMQTT::_username;
string          AsyncMQTT::_password;
string          AsyncMQTT::_willTopic;
string          AsyncMQTT::_willPayload;
uint8_t         AsyncMQTT::_willQos;
bool            AsyncMQTT::_willRetain;
uint16_t        AsyncMQTT::_keepalive=15 * ASMQ_POLL_RATE; // 10 seconds
bool            AsyncMQTT::_cleanSession;
string          AsyncMQTT::_clientId;
uint16_t        AsyncMQTT::_maxRetries=ASMQ_MAX_RETRIES; 

AsyncMQTT::AsyncMQTT() {
    _caller=this;
    AsyncClient::onConnect([this](void* obj, AsyncClient* c) { new ConnectPacket(); });
    AsyncClient::onDisconnect([this](void* obj, AsyncClient* c) { _onDisconnect(TCP_DISCONNECTED); });
    //AsyncClient::onError([this](void* obj, AsyncClient* c, int8_t error) {  });
    AsyncClient::onData([this](void* obj, AsyncClient* c, void* data, size_t len) { _onData(static_cast<uint8_t*>(data), len,false); });
    AsyncClient::onPoll([this](void* obj, AsyncClient* c) { _onPoll(c); });
#ifdef ARDUINO_ARCH_ESP32
  sprintf(_generatedClientId, "esp32-%06llx", ESP.getEfuseMac());
#elif defined(ARDUINO_ARCH_ESP8266)
  sprintf(_generatedClientId, "esp8266-%06x", ESP.getChipId());
#endif
  _clientId = _generatedClientId;
}

void AsyncMQTT::setCredentials(const char* username, const char* password) {
  _username = username;
  _password = password;
}

void AsyncMQTT::setWill(const char* topic, uint8_t qos, bool retain, const char* payload) {
  _willTopic = topic;
  _willQos = qos;
  _willRetain = retain;
  _willPayload = payload;
}

void AsyncMQTT::setServer(IPAddress ip, uint16_t port) {
  _useIp = true;
  _ip = ip;
  _port = port;
}

void AsyncMQTT::setServer(const char* host, uint16_t port) {
  _useIp = false;
  _host = host;
  _port = port;
}

void AsyncMQTT::_onDisconnect(uint8_t r) {
    _connected = false;
    if(_cbDisconnect) _cbDisconnect(r);
}

uint8_t* AsyncMQTT::_incomingPacket(uint8_t type,struct ASMQ_PROPS props,uint8_t* data, uint16_t len,bool synthetic){
    uint16_t    id=0;
    switch (type){
        case CONNACK:
            if(data[1]) _onDisconnect(data[1]);
            else {
                _connected = true;
                bool session=data[0] & 0x01;
                if(!session) _cleanStart();
                else Packet::_clearPacketMap([](PublishPacket* pub){ return pub->resend(); });
                if(_cbConnect) _cbConnect(session);
            }
            break;
        case PINGRESP:
            _nSrvTicks=0;
            break;
        case SUBACK:
            id=_peek16(data);
            Packet::ACKoutbound(id);
            if(_cbSubscribe) _cbSubscribe(id,props.qos);
            break;
        case UNSUBACK:
            id=_peek16(data);
            Packet::ACKoutbound(id);
            if(_cbUnsubscribe) _cbUnsubscribe(id);
            break;
        case PUBACK:
            id=_peek16(data);
            Packet::ACKoutbound(id);
            if(_cbPublish) _cbPublish(id);
            break;
        case PUBREC:
            id=_peek16(data);
            new PubrelPacket(id); // self destructs
            break;
        case PUBREL:
            id=_peek16(data);
            new PubcompPacket(id); // pubrel
            if(Packet::_inbound.count(id)){
                PublishPacket* pub=reinterpret_cast<PublishPacket*>(Packet::_inbound[id]);
                _onData(pub->_blox.front().second,pub->_blox.front().first,true); // synthetic
            }
            break;
        case PUBCOMP:
            id=_peek16(data);
            Packet::ACKoutbound(id);
            if(_cbPublish) _cbPublish(id);
            break;
        default:
            if(type & 0xf0 != 0x30) break;
            {
            uint8_t*    p=data;
            uint16_t    id=0;
            uint16_t    tlen=_peek16(p);p+=2;

            char c_topic[tlen+1];
            memcpy(&c_topic[0],p,tlen);c_topic[tlen]='\0';
            p+=tlen;
            if(props.qos) {
                id=_peek16(p);p+=2;
            }
            uint16_t plen=&data[0]+len-p;

            if(synthetic){ // <-- from pubcomp
                Packet::ACKinbound(id); // inbound!!!
                if(_cbMessage) _cbMessage(c_topic, p, props, plen, 0, plen);
            }
            else {
                switch(props.qos){
                    case 0:
                        if(_cbMessage) _cbMessage(c_topic, p, props, plen, 0, plen);
                        break;
                    case 1:
                        new PubackPacket(id); // self destructs
                        if(_cbMessage) _cbMessage(c_topic, p, props, plen, 0, plen);
                        break;
                    case 2:
                    //  MQTT Spec. "method A"
                        new PubrecPacket(id); // pubrel
                        new PublishPacket(c_topic,props.qos,props.retain,p,plen,props.dup,id); // build and HOLD until PUBREL
                        break;
                }
            }
            break;
            }
    }
    return data+len;
}

void AsyncMQTT::_onData(uint8_t* data, size_t len,bool synthetic) {
    uint8_t*    p=data;
    do {
        uint8_t             type=p[0];
        uint8_t             bits=type & 0x0f;
        struct ASMQ_PROPS   props={ (bits & 0x6) >> 1, (bits & 0x8) >> 3, bits & 0x1};
        pair<uint32_t,uint8_t>grl=Packet::_getrl(&p[1]); // tidy into this
        p=_incomingPacket(type,props,&p[1]+grl.second,grl.first,synthetic);
    } while (&data[0] + len - p);
}

void AsyncMQTT::_onPoll(AsyncClient* client) {
    if(_connected){
        ++_nPollTicks;
        ++_nSrvTicks;
        if(_nSrvTicks > ((_keepalive * 5) / 4)) _onDisconnect(MQTT_SERVER_UNAVAILABLE);
        else {
            if(_nPollTicks > _keepalive){
                _clearGarbage();
                Packet::_clearPacketMap([](PublishPacket* pub){ return pub->resend(); });
                new PingPacket(); // self-destructs
                _nPollTicks=0;
            }
        }
    }
}

void AsyncMQTT::connect() {
    if (_connected) return;
    _clearGarbage();
    if (_useIp) AsyncClient::connect(_ip, _port);
    else AsyncClient::connect(CSTR(_host), _port);
}

void AsyncMQTT::disconnect(bool force) {
    if (!_connected) return;
    if (force) close(true);
    else new DisconnectPacket();
}

uint16_t AsyncMQTT::subscribe(const char* topic, uint8_t qos) {
    if (!_connected) return 0;
    SubscribePacket* p=new SubscribePacket(topic,qos);
    return p->packetId();
}

uint16_t AsyncMQTT::unsubscribe(const char* topic) {
    if (!_connected) return 0;
    UnsubscribePacket* p=new UnsubscribePacket(topic);
    return p->packetId();
}

uint16_t AsyncMQTT::publish(const char* topic, uint8_t qos, bool retain, uint8_t* payload, size_t length, bool dup) {
    if (!_connected) return 0;
    PublishPacket* p=new PublishPacket(topic,qos,retain,payload,length,dup);
    return p->packetId();
}

void AsyncMQTT::_clearGarbage(){
    for(auto const& g:Packet::_garbage) delete g;
    Packet::_garbage.clear();
    Packet::_garbage.shrink_to_fit();
}

void AsyncMQTT::_cleanStart(){
    Packet::_clearPacketMap([](PublishPacket*){ return true; }); // unconditional
    Packet::_nextId=0;
}

void AsyncMQTT::dumphex(const void *mem, uint32_t len, uint8_t cols) {
    const uint8_t* src = (const uint8_t*) mem;
    ASMQ_PRINT("Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
    for(uint32_t i = 0; i < len; i++) {
        if(i % cols == 0) {
            ASMQ_PRINT("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
        }
        ASMQ_PRINT("%02X ", *src);
        src++;
    }
    ASMQ_PRINT("\n");
}

#ifdef ASYNC_MQTT_DEBUG
void AsyncMQTT::dump(){ 
    ASMQ_PRINT("DUMP ALL %d PACKETS OUTBOUND\n",Packet::_outbound.size());
    for(auto const& p:Packet::_outbound){
        ASMQ_PRINT("CALL _dump for %08x\n",(void*) p.second);
        p.second->_dump();
    }
    ASMQ_PRINT("DUMP ALL %d PACKETS INBOUND\n",Packet::_inbound.size());
    for(auto const& p:Packet::_inbound) p.second->_dump();
}
#endif
#include <AsyncMQTT.h>
#include <Packet.h>

AsyncClient*             _caller;

std::string          AsyncMQTT::_username;
std::string          AsyncMQTT::_password;
std::string          AsyncMQTT::_willTopic;
std::string          AsyncMQTT::_willPayload;
uint8_t              AsyncMQTT::_willQos;
bool                 AsyncMQTT::_willRetain;
uint16_t             AsyncMQTT::_keepalive=15 * ASMQ_POLL_RATE; // 10 seconds
bool                 AsyncMQTT::_cleanSession;
std::string          AsyncMQTT::_clientId;
uint16_t             AsyncMQTT::_maxRetries=ASMQ_MAX_RETRIES; 
// optimised packets
uint8_t              AsyncMQTT::staticPing[]={PINGREQ,2};
uint8_t              AsyncMQTT::staticDisco[]={DISCONNECT,2};

namespace ASMQ {
    void dumphex(const void *mem, uint32_t len, uint8_t cols) {
        const uint8_t* src = (const uint8_t*) mem;
        ASMQ_PRINT("Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
        for(uint32_t i = 0; i < len; i++) {
            if(i % cols == 0) ASMQ_PRINT("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
            ASMQ_PRINT("%02X ", *src);
            src++;
        }
        ASMQ_PRINT("\n");
    }
}

AsyncMQTT::AsyncMQTT() {
    _caller=this;
    AsyncClient::onConnect([this](void* obj, AsyncClient* c) { ConnectPacket cp{}; }); // *NOT* A MEMORY LEAK! :)
    AsyncClient::onDisconnect([this](void* obj, AsyncClient* c) { _onDisconnect(TCP_DISCONNECTED); });
    AsyncClient::onAck([this](void* obj, AsyncClient* c,size_t len, uint32_t time){ Packet::_ackTCP(len,time); }); 
    AsyncClient::onError([this](void* obj, AsyncClient* c, int8_t error) {  });
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

void AsyncMQTT::_incomingPacket(uint8_t* data, uint8_t offset,uint32_t pktlen,bool synthetic){
    uint16_t    id=0;
    uint8_t*    i=data+1+offset;
//    ASMQ_PRINT("INBOUND type %02x\n",data[0]);
//    AsyncMQTT::dumphex(data,pktlen);
    switch (data[0]){
        case CONNACK:
            if(i[1]) _onDisconnect(i[1]);
            else {
                _connected = true;
                bool session=i[0] & 0x01;
                if(!session) _cleanStart();
                else Packet::_clearPacketMap(Packet::_predInbound,Packet::_predOutbound);
                if(_cbConnect) _cbConnect(session);
            }
            break;
        case PINGRESP:
            _nSrvTicks=0;
            break;
        case SUBACK:
            if(_cbSubscribe) _cbSubscribe(_peek16(i),(data[0] & 0x6) >> 3);
            break;
        case UNSUBACK:
            if(_cbUnsubscribe) _cbUnsubscribe(_peek16(i));
            break;
        case PUBACK:
            if(_cbPublish) _cbPublish(_peek16(i));
            break;
        case PUBREC:
            { PubrelPacket prp(_peek16(i)); }
            break;
        case PUBREL:
            {
                PubcompPacket pcp(_peek16(i)); // pubrel
                if(Packet::_inbound.count(id)) _onData(Packet::_inbound[id],Packet::_packetLength(Packet::_inbound[id]),true); // synthetic
            }
            break;
        case PUBCOMP:
            id=_peek16(i);
            if(_cbPublish) _cbPublish(_peek16(i));
            break;
        default:
            if((data[0] & 0xf0) == 0x30) {
                ADP_t dp=Packet::_decodePub(data,offset,pktlen);
                if(synthetic){ // <-- from pubrel
                    Packet::ACKinbound(dp.id);
                    if(_cbMessage) _cbMessage(dp.topic.c_str(), dp.payload, ASMQ_PROPS_t {dp.qos,dp.dup,dp.retain}, dp.plen, 0, dp.plen);
                }
                else {
                    switch(dp.qos){
                        case 0:
                            if(_cbMessage) _cbMessage(dp.topic.c_str(), dp.payload, ASMQ_PROPS_t {dp.qos,dp.dup,dp.retain}, dp.plen, 0, dp.plen);
                            break;
                        case 1:
                            {
                                PubackPacket pap(dp.id); // self destructs
                                if(_cbMessage) _cbMessage(dp.topic.c_str(), dp.payload, ASMQ_PROPS_t {dp.qos,dp.dup,dp.retain}, dp.plen, 0, dp.plen);
                            }
                            break;
                        case 2:
                        //  MQTT Spec. "method A"
                            {
                                PubrecPacket pcp(dp.id); // pubrel
                                PublishPacket pub(dp.topic.c_str(),dp.qos,dp.retain,dp.payload,dp.plen,dp.dup,dp.id); // build and HOLD until PUBREL
                            }
                            break;
                    }
                }
            }
            break;
    }
}

void AsyncMQTT::_onData(uint8_t* data, size_t len,bool synthetic) {
    uint8_t*    p=data;
    do {
        std::pair<uint32_t,uint8_t>grl=Packet::_getrl(&p[1]); // tidy into this
        _incomingPacket(data,grl.second,1+grl.second+grl.first,synthetic);
        p+=(1+grl.second+grl.first);
    } while (&data[0] + len - p);
}

void AsyncMQTT::_onPoll(AsyncClient* client) {
    if(_connected){
        ++_nPollTicks;
        ++_nSrvTicks;
//        Packet::_clearPacketMap(Packet::_predInbound,Packet::_predOutbound);
        if(_nSrvTicks > ((_keepalive * 5) / 4)) _onDisconnect(MQTT_SERVER_UNAVAILABLE);
        else {
            if(_nPollTicks > _keepalive){
                PingPacket pp{};//Packet::_release(&staticPing[0],2);
                _nPollTicks=0;
                Packet::_clearPacketMap(Packet::_predInbound,Packet::_predOutbound);
            }
        }
    }
}

void AsyncMQTT::connect() {
    if (_connected) return;
    if (_useIp) AsyncClient::connect(_ip, _port);
    else AsyncClient::connect(CSTR(_host), _port);
}

void AsyncMQTT::disconnect(bool force) {
    if (!_connected) return;
    if (force) close(true);
    else  DisconnectPacket dp{}; //Packet::_release(&staticDisco[0],2);
}

uint16_t AsyncMQTT::subscribe(const char* topic, uint8_t qos) {
    if (!_connected) return 0;
    SubscribePacket sub(topic,qos);
    return sub.packetId();
}

uint16_t AsyncMQTT::unsubscribe(const char* topic) {
    if (!_connected) return 0;
    UnsubscribePacket usp(topic);
    return usp.packetId();
}

uint16_t AsyncMQTT::publish(const char* topic, uint8_t qos, bool retain, uint8_t* payload, size_t length, bool dup) {
    if (!_connected) return 0;
    PublishPacket pub(topic,qos,retain,payload,length,dup);
    return pub.packetId();
}

void AsyncMQTT::_cleanStart(){
    Packet::_clearPacketMap([](ADFP){ return true; },[](ADFP){ return true; }); // unconditional
    Packet::_nextId=0;
}

void AsyncMQTT::dumphex(const void *mem, uint32_t len, uint8_t cols) {
    const uint8_t* src = (const uint8_t*) mem;
    ASMQ_PRINT("Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
    for(uint32_t i = 0; i < len; i++) {
        if(i % cols == 0) ASMQ_PRINT("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
        ASMQ_PRINT("%02X ", *src);
        src++;
    }
    ASMQ_PRINT("\n");
}

#ifdef ASYNC_MQTT_DEBUG
void AsyncMQTT::dump(){ 
    //ASMQ_PRINT("DUMP ALL %d PACKETS OUTBOUND\n",Packet::_outbound.size());
    for(auto const& p:Packet::_outbound){
        ASMQ_PRINT("Type %02x id=%d\n",p.second[0],p.first);
        dumphex(p.second,Packet::_packetLength(p.second));
    }

    //ASMQ_PRINT("DUMP ALL %d PACKETS INBOUND\n",Packet::_inbound.size());
    for(auto const& p:Packet::_inbound) {
        ASMQ_PRINT("Type %02x id=%d\n",p.second[0],p.first);
        dumphex(p.second,Packet::_packetLength(p.second));
    }
    ASMQ_PRINT("\n");
}
#endif
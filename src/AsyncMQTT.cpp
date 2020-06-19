#include <AsyncMQTT.h>
#include <Packet.h>

AsyncClient*                    _caller;

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
uint32_t             AsyncMQTT::_nPollTicks=0;

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
void AsyncMQTT::_createClient(){
    ASMQ_PRINT("****** CREATE CLIENT *****************\n");
    _caller=new AsyncClient;
    _caller->setRxTimeout(5000);
    _caller->setAckTimeout(10000);
    _caller->setNoDelay(true);

    ASMQ_PRINT("TCP: MSS=%d RxT=%d AckT=%d Ndly=%d\n",
        _caller->getMss(),
        _caller->getRxTimeout(),
        _caller->getAckTimeout(),
        _caller->getNoDelay()
    );

    _caller->onConnect([this](void* obj, AsyncClient* c) {Packet::_pcb_busy = false; ConnectPacket cp{}; }); // *NOT* A MEMORY LEAK! :)
    _caller->onDisconnect([this](void* obj, AsyncClient* c) { _onDisconnect(66); });
    _caller->onAck([this](void* obj, AsyncClient* c,size_t len, uint32_t time){ Packet::_ackTCP(len,time); }); 
//    _caller->onError([this](void* obj, AsyncClient* c, int8_t error) { _onDisconnect(77); });
//    _caller->onTimeout([this](void* obj, AsyncClient* c, uint32_t time) { _onDisconnect(88); });
    _caller->onTimeout([this](void* obj, AsyncClient* c, uint32_t time) { _onTimeout(time);});
    _caller->onData([this](void* obj, AsyncClient* c, void* data, size_t len) { _onData(static_cast<uint8_t*>(data), len,false); });
    _caller->onPoll([this](void* obj, AsyncClient* c) { _onPoll(c); });
}

void AsyncMQTT::_destroyClient(){
    ASMQ_PRINT("****** DESTROY CLIENT *****************\n");
    if(_caller) {
        _connected = false;
        _caller->stop();
        delete _caller;
        _caller=nullptr;
        ASMQ_PRINT("****** DESTROYED CLIENT *****************\n");
    } else ASMQ_PRINT("****** CLIENT ALREADY DEAD *****************\n");
}

AsyncMQTT::AsyncMQTT() {

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

void AsyncMQTT::_onDisconnect(int8_t r) {
    ASMQ_PRINT("_onDisconnect %d\n",r);
    if(_connected){
        Packet::_clearPacketMap(Packet::_predInbound,Packet::_predOutbound);
        Packet::_clearFlowControlQ();
        ASMQ_PRINT("DELETE ????? %d\n",r);
        _destroyClient();
        if(_cbDisconnect) _cbDisconnect(r);
    }
}

void AsyncMQTT::_incomingPacket(uint8_t* data, uint8_t offset,uint32_t pktlen,bool synthetic){
    uint16_t    id=0;
    uint8_t*    i=data+1+offset;
//    ASMQ_PRINT("INBOUND type %02x\n",data[0]);
//    AsyncMQTT::dumphex(data,pktlen);
    if(!synthetic) _nSrvTicks=0;
    switch (data[0]){
        case CONNACK:
            if(Packet::_flowControl.size()){
                ASMQ_PRINT("CONNACK flow=%d inflight=%08x %02x\n",Packet::_flowControl.size(),Packet::_flowControl.front(),Packet::_flowControl.front()[0]);
            }
            if(i[1]) _onDisconnect(i[1]);
            else {
                _connected = true;
                _nPollTicks=_nSrvTicks=_nPingTicks=0;
                bool session=i[0] & 0x01;
                if(!session) _cleanStart();
                else Packet::_clearPacketMap(Packet::_predInbound,Packet::_predOutbound);
                if(_cbConnect) _cbConnect(session);
            }
            break;
        case PINGRESP:
            _nPingTicks=0;
            _PingSent = false;
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
                            //ASMQ_PRINT("DECODE %s id=%d p=%08x plen=%d q=%d r=%d d=%d\n",CSTR(dp.topic),dp.id,dp.payload,dp.plen,dp.qos,dp.retain,dp.dup);
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
        ASMQ_REM_LENGTH grl=Packet::_getrl(&p[1]); // tidy into this
        size_t N=1+grl.second+grl.first;
        _incomingPacket(data,grl.second,N,synthetic);
        p+=N;
    } while (&data[0] + len - p);
}

#define CLEAR_PACKET_TICKS (5 * ASMQ_POLL_RATE)  // Clear Packets after server idle for 5 seconds

void AsyncMQTT::_onPoll(AsyncClient* client) {
    ASMQ_PRINT("T=%u poll C=%d S=%d P=%d\n",millis(),_nPollTicks,_nSrvTicks,_nPingTicks);
    if(_connected){
        uint32_t _pollserver = (_keepalive > CLEAR_PACKET_TICKS) ? _keepalive : CLEAR_PACKET_TICKS;

        ++_nPollTicks;
        ++_nSrvTicks;
        if (_PingSent) ++_nPingTicks;

        if(_nPingTicks > ((_keepalive * 3) / 2)) _onDisconnect(55);  // No PINGRESP received
        else if(_nPollTicks > _keepalive){  // No client send activity
            PingPacket pp{};
            _PingSent = true;
            _nPollTicks=0;
        }
        else if(_nSrvTicks > _pollserver){  // No server receive activity
            PingPacket pp{};  // Is server still alive ?
            _PingSent = true;
            _nSrvTicks=0;
        }
        else if(_nSrvTicks == CLEAR_PACKET_TICKS) Packet::_clearPacketMap(Packet::_predInbound,Packet::_predOutbound); // Last packet received 5 seconds ago 
    }
}

void AsyncMQTT::_onTimeout(uint32_t time){
    Packet::_pcb_busy = false;
    ASMQ_PRINT("Ack-Timeout %u\n", time);

    if(Packet::_unAcked){
        ASMQ_PRINT("TCP ACK len=%d time=%d ua=%08x typ=%02x uaId=%d\n",time,0,Packet::_unAcked,Packet::_unAcked[0],Packet::_uaId);
        if(((Packet::_unAcked[0] & 0xf0) == 0x30) && Packet::_uaId ){
            ASMQ_PRINT("QOS PUBLISH - DO NOT KILL!\n"); // the ptr is left hanging but lives in either _inbound or _outbound
        }
        else {
//            ASMQ_PRINT("GARBAGE: KILL %08x\n",_unAcked);
            free(Packet::_unAcked);
            Packet::_unAcked=nullptr;
            Packet::_uaId=0;
        }
    } else ASMQ_PRINT("FATAL!: ACK WITH NO OUTBOUND !\n");

    _onDisconnect(88);  // will clear both _flowControl and PacketMaps
}


void AsyncMQTT::connect() {
    if (_connected) return;
    _createClient();

    _nPollTicks=_nSrvTicks=_nPingTicks=0;
    ASMQ_PRINT("CONNECT");
    if (_useIp) _caller->connect(_ip, _port);
    else _caller->connect(CSTR(_host), _port);
}

void AsyncMQTT::disconnect(bool force) {
    if (!_connected) return;

    if (force) _caller->close(true);
    else DisconnectPacket dp{};
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
    Packet::_clearFlowControlQ();
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
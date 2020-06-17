#pragma once

#include"config.h"
#include<Arduino.h>
#include<functional>
#include<string>
#include<map>

#ifdef ARDUINO_ARCH_ESP32
#include <AsyncTCP.h>
#elif defined(ARDUINO_ARCH_ESP8266)
#include <ESPAsyncTCP.h>
#elif defined(ARDUINO_ARCH_STM32)
#include <STM32AsyncTCP.h>
#else
#error Platform not supported
#endif

#ifdef ASYNC_MQTT_DEBUG
    #define ASMQ_PRINT(...) Serial.printf(__VA_ARGS__)
#else
    #define ASMQ_PRINT(...)
#endif

#define CSTR(x) x.c_str()
enum :uint8_t {
    CONNECT     = 0x10, // x
    CONNACK     = 0x20, // x
    PUBLISH     = 0x30, // x
    PUBACK      = 0x40, // x
    PUBREC      = 0x50, 
    PUBREL      = 0x62,
    PUBCOMP     = 0x70,
    SUBSCRIBE   = 0x82, // x
    SUBACK      = 0x90, // x
    UNSUBSCRIBE = 0xa2, // x
    UNSUBACK    = 0xb0, // x
    PINGREQ     = 0xc0, // x
    PINGRESP    = 0xd0, // x
    DISCONNECT  = 0xe0
};

enum : int8_t {
  TCP_DISCONNECTED = 0,
//  MQTT_UNACCEPTABLE_PROTOCOL_VERSION = 1,
  MQTT_IDENTIFIER_REJECTED = 2,
  MQTT_SERVER_UNAVAILABLE = 3,
  MQTT_MALFORMED_CREDENTIALS = 4,
  MQTT_NOT_AUTHORIZED = 5,
//  ESP8266_NOT_ENOUGH_SPACE = 6,
  TLS_BAD_FINGERPRINT = 7,
  TCP_TIMEOUT,
  FORCED_BY_USER
};

struct ASMQ_PROPS {
  uint8_t qos;
  bool dup;
  bool retain;
};

using ASMQ_PROPS_t              = struct ASMQ_PROPS;

using AsyncMQTT_cbConnect       =std::function<void(bool)>;
using AsyncMQTT_cbDisconnect    =std::function<void(int8_t)>;
using AsyncMQTT_cbSubscribe     =std::function<void(uint16_t, uint8_t)>;
using AsyncMQTT_cbUnsubscribe   =std::function<void(uint16_t)>;
using AsyncMQTT_cbMessage       =std::function<void(const char*, uint8_t*, ASMQ_PROPS_t , size_t, size_t, size_t)>;
using AsyncMQTT_cbPublish       =std::function<void(uint16_t packetId)>;

class Packet;
class ConnectPacket;
class PublishPacket;

struct ASMQ_DECODED_PUB {
    uint16_t        id;
    uint8_t         qos;
    bool            dup;
    bool            retain;
    std::string     topic;
    uint8_t*        payload;
    uint32_t        plen;
};
using ADP_t         = struct ASMQ_DECODED_PUB;

class AsyncMQTT {
        friend class Packet;
        friend class ConnectPacket;
        friend class PublishPacket;
        
        AsyncMQTT_cbConnect     _cbConnect=nullptr;
        AsyncMQTT_cbDisconnect  _cbDisconnect=nullptr;
        AsyncMQTT_cbSubscribe   _cbSubscribe=nullptr;
        AsyncMQTT_cbUnsubscribe _cbUnsubscribe=nullptr;
        AsyncMQTT_cbMessage     _cbMessage=nullptr;
        AsyncMQTT_cbPublish     _cbPublish=nullptr;

        static bool            _cleanSession;
        static std::string     _clientId;
               bool            _connected=false;
               char            _generatedClientId[19];  // esp8266-abc123 and esp32-abcdef123456 
               std::string     _host;
               IPAddress       _ip;
        static uint16_t        _keepalive;
        static uint16_t        _maxRetries; 
               uint32_t        _nPollTicks=0;  
               uint32_t        _nSrvTicks=0;  
        static std::string     _password;
               uint16_t        _port;
               bool            _useIp;
        static std::string     _username;
        static std::string     _willPayload;
        static uint8_t         _willQos;
        static bool            _willRetain;
        static std::string     _willTopic;

               void            _cleanStart();
               void            _createClient();
               void            _destroyClient();
               void            _incomingPacket(uint8_t* data, uint8_t offset,uint32_t pktlen,bool synthetic);
        static uint16_t        _peek16(uint8_t* p){ return (*(p+1))|(*p << 8); }
        // TCP
               void            _onData(uint8_t* data, size_t len,bool synthetic=false);
               void            _onDisconnect(int8_t r);
               void            _onPoll(AsyncClient* client);
               void            _onTimeout(uint32_t time);
    public:
        AsyncMQTT();

               void            onConnect(AsyncMQTT_cbConnect callback){ _cbConnect=callback; }
               void            onDisconnect(AsyncMQTT_cbDisconnect callback){ _cbDisconnect=callback; }
               void            onSubscribe(AsyncMQTT_cbSubscribe callback){ _cbSubscribe=callback; }
               void            onUnsubscribe(AsyncMQTT_cbUnsubscribe callback){ _cbUnsubscribe=callback; }
               void            onMessage(AsyncMQTT_cbMessage callback){ _cbMessage=callback; }
               void            onPublish(AsyncMQTT_cbPublish callback){ _cbPublish=callback; }

               void            setKeepAlive(uint16_t keepAlive){ _keepalive = ASMQ_POLL_RATE * keepAlive; }
               void            setClientId(const char* clientId){ _clientId = clientId; }
               void            setCleanSession(bool cleanSession){ _cleanSession = cleanSession; }
               void            setMaxRetries(uint16_t nRetries){ _maxRetries=nRetries; };
               void            setCredentials(const char* username, const char* password = nullptr);
               void            setWill(const char* topic, uint8_t qos, bool retain, const char* payload = nullptr);
               void            setServer(IPAddress ip, uint16_t port);
               void            setServer(const char* host, uint16_t port);

               void            connect();
               bool            connected(){ return _connected; };
               void            disconnect(bool force = false);
        static void            dumphex(const void *mem, uint32_t len, uint8_t cols=16);
               const char*     getClientId(){ return _clientId.c_str(); }
               uint16_t        publish(const char* topic, uint8_t qos, bool retain, uint8_t* payload = nullptr, size_t length = 0, bool dup = false);
               uint16_t        publish(const char* topic, uint8_t qos, bool retain, std::string payload){ return publish(topic,qos,retain, (uint8_t*) payload.data(), payload.size()); }
               uint16_t        subscribe(const char* topic, uint8_t qos);
               uint16_t        unsubscribe(const char* topic);

#ifdef ASYNC_MQTT_DEBUG
        static void            dump();
#endif
};

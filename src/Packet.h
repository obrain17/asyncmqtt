#pragma once
#include <AsyncMQTT.h>
#include <queue>

enum ASYNC_MQTT_CNX_FLAG : uint8_t {
    USERNAME      = 0x80,
    PASSWORD      = 0x40,
    WILL_RETAIN   = 0x20,
    WILL_QOS0     = 0x00,
    WILL_QOS1     = 0x08,
    WILL_QOS2     = 0x10,
    WILL          = 0x04,
    CLEAN_SESSION = 0x02
};

extern AsyncClient*     _caller;
//
//  A pointer to a malloc'd block that contains the whole actual packet data.
//  It will NOT get free'd until the TCP ack comes back for it
//  so anywhere one is used it will LOOK like a memory leak, but IT IS NOT!
//  
//  1:1 flow control of packets sent vs packets ACKed is enforced to avoid overlap
//  and ensure incoming TCP ACK matches the last packet sent which is held in _unAcked
//  with its id in _uaId (acting together as a kind of singleton ASMQ_PACKET_MAP)
//
using ASMQ_DELAYED_FREE   = uint8_t*;
using ADFP                 = ASMQ_DELAYED_FREE; // SOOO much less typing - ASMQ "delayed free" pointer

using ASYNC_MQTT_BLOCK    = std::pair<size_t,uint8_t*>;
using ASMQ_FN_VOID        = std::function<void(void)>;
using ASMQ_FN_U8PTR       = std::function<void(uint8_t*,ADFP base)>;
using ASMQ_FN_U8PTRU8     = std::function<uint8_t*(uint8_t*)>;
using ASMQ_PACKET_MAP     = std::map<uint16_t,ADFP>; // indexed by messageId
using ASMQ_BLOCK_Q        = std::queue<ASYNC_MQTT_BLOCK>;
using ASMQ_RESEND_PRED    = std::function<bool(ADFP)>;
using ASMQ_REM_LENGTH     = std::pair<uint32_t,uint8_t>;

class Packet {
    friend class AsyncMQTT;
        static  std::queue<ADFP>  _flowControl;
    protected:
        static  ADFP             _unAcked;
        static  uint16_t         _uaId;
        static  uint16_t         _nextId;
                uint16_t         _id=0; 
        static  ASMQ_PACKET_MAP  _outbound;
        static  ASMQ_PACKET_MAP  _inbound;
                uint8_t          _hdrAdjust;
                bool             _hasId=false;
                uint8_t          _controlcode;
                ASMQ_BLOCK_Q     _blox;
                uint32_t         _bs=0;
                ASMQ_FN_VOID     _begin=[]{};
                ASMQ_FN_U8PTRU8  _middle=[](uint8_t* p){ return p; };
                ASMQ_FN_U8PTR    _end=[](uint8_t* p,ADFP base){};

        static  void             _ackTCP(size_t len, uint32_t time);
        static  void             _ACK(ASMQ_PACKET_MAP* m,uint16_t id);
                uint8_t*         _block(size_t size);
                void	         _build(bool hold=false);
        static  void             _clearFlowControlQ();
        static  void             _clearMap(ASMQ_PACKET_MAP* m,ASMQ_RESEND_PRED pred);
        static  void             _clearPacketMap(ASMQ_RESEND_PRED ipred,ASMQ_RESEND_PRED opred);
        static  ASMQ_REM_LENGTH  _getrl(uint8_t* p);
        static  ADP_t            _decodePub(uint8_t* data,uint8_t offset,uint32_t length);
                void             _idGarbage(uint16_t id);
                void             _initId();
                uint8_t*         _mem(const void* v,size_t size);
        static  uint32_t         _packetLength(uint8_t* p);
                uint8_t*         _poke16(uint8_t* p,uint16_t u);
        static  bool             _predInbound(ADFP p);
        static  bool             _predOutbound(ADFP p);
        static  void             _release(uint8_t* base,size_t len);
                std::vector<uint8_t>  _rl(uint32_t X);
                void             _shortGarbage();
                uint8_t*         _stringblock(const std::string& s){ return _mem(s.data(),s.size()); }
            
    public:
        Packet(uint8_t controlcode,uint8_t adj=0,bool hasid=false): _controlcode(controlcode),_hdrAdjust(adj),_hasId(hasid){}
        virtual ~Packet();

    static  void        ACKinbound(uint16_t id){ _ACK(&_inbound,id); }
    static  void        ACKoutbound(uint16_t id){ _ACK(&_outbound,id); }
            uint16_t    packetId(){ return _id; }
};
class ConnectPacket: public Packet {
            uint8_t  protocol[8]={0x0,0x4,'M','Q','T','T',4,0}; // 3.1.1
    public:
        ConnectPacket();
};
class PingPacket: public Packet {
    public:
        PingPacket(): Packet(PINGREQ) { _shortGarbage(); }
};
class DisconnectPacket: public Packet {
    public:
        DisconnectPacket(): Packet(DISCONNECT) { _shortGarbage(); }
};
class PubackPacket: public Packet {
    public:
        PubackPacket(uint16_t id): Packet(PUBACK) { _idGarbage(id); }
};
class PubrecPacket: public Packet {
    public:
        PubrecPacket(uint16_t id): Packet(PUBREC) { _idGarbage(id); }
};
class PubrelPacket: public Packet {
    public:
        PubrelPacket(uint16_t id): Packet(PUBREL) { _idGarbage(id); }
};
class PubcompPacket: public Packet {
    public:
        PubcompPacket(uint16_t id): Packet(PUBCOMP) { _idGarbage(id); }  
};
class SubscribePacket: public Packet {
        std::string          _topic;
    public:
        uint8_t         _qos;
        SubscribePacket(const std::string& topic,uint8_t qos): _topic(topic),_qos(qos),Packet(SUBSCRIBE,1,true) {
            _id=++_nextId;
            _begin=[this]{ _stringblock(CSTR(_topic)); };
            _end=[this](uint8_t* p,ADFP base){ *p=_qos; };
            _build();
        }
};
class UnsubscribePacket: public Packet {
        std::string          _topic;
    public:
        UnsubscribePacket(const std::string& topic): _topic(topic),Packet(UNSUBSCRIBE,1,false) {
            _id=++_nextId;
            _begin=[this]{ _stringblock(CSTR(_topic)); };
            _build();
        }
};

class PublishPacket: public Packet {
        std::string     _topic;
        uint8_t         _qos;
        bool            _retain;
        size_t          _length;
        bool            _dup;
        uint8_t         _retries;
        uint16_t        _givenId=0;
    public:
        PublishPacket(const char* topic, uint8_t qos, bool retain, uint8_t* payload, size_t length, bool dup,uint16_t givenId=0);
        ~PublishPacket(){ _outbound.erase(_id); }

        bool     resend();
};

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
    CLEAN_SESSION = 0x02,
    CNX_RESERVED  = 0x00,
};

extern AsyncClient*     _caller;

using ASYNC_MQTT_BLOCK    = pair<size_t,uint8_t*>;
using ASMQ_FN_VOID        = function<void(void)>;
using ASMQ_FN_U8PTR       = function<void(uint8_t*)>;
using ASMQ_FN_U8PTRU8     = function<uint8_t*(uint8_t*)>;
using ASMQ_PACKET_MAP     = std::map<uint8_t,Packet*>;
using ASMQ_BLOCK_Q        = queue<ASYNC_MQTT_BLOCK>;

class Packet {
    friend class AsyncMQTT;
        static  vector<Packet*>  _garbage;
        static  uint16_t         _nextId;
    protected:
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
                ASMQ_FN_U8PTR    _end=[](uint8_t* p){};

        static  void             _ACK(ASMQ_PACKET_MAP* m,uint16_t id);
                uint8_t*         _block(size_t size);
                void	         _build(bool hold=false);
        static  void             _clearMap(ASMQ_PACKET_MAP* m,function<bool(PublishPacket*)> pred);
        static  void             _clearPacketMap(function<bool(PublishPacket* p)> pred);
        static  pair<uint32_t,uint8_t> _getrl(uint8_t* p);
                void             _idGarbage(uint16_t id);
                void             _initGC(){ _garbage.push_back(this); }
                void             _initId();
                uint8_t*         _mem(const void* v,size_t size);
                uint8_t*         _poke16(uint8_t* p,uint16_t u);
                vector<uint8_t>  _rl(uint32_t X);
                void             _shortGarbage();
                uint8_t*         _stringblock(const string& s){ return _mem(s.data(),s.size()); }

                void        _release(uint8_t* base,size_t len);
            
                #ifdef ASYNC_MQTT_DEBUG
                void        _dump(){
                    ASMQ_PRINT("\ndump Packet %02x id=%d\n",_controlcode,_id);
                    queue<ASYNC_MQTT_BLOCK> cq=_blox;
                    while(!cq.empty()){
                        ASMQ_PRINT("blok %08x %d\n",(void*) cq.front().second,cq.front().first);
                        AsyncMQTT::dumphex(cq.front().second,cq.front().first);
                        cq.pop();
                    }
                    ASMQ_PRINT("\n");
                }
                #endif
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
        string          _topic;
    public:
        uint8_t         _qos;
        SubscribePacket(const string& topic,uint8_t qos): _topic(topic),_qos(qos),Packet(SUBSCRIBE,1,true) {
            _initId();
            _begin=[this]{ _stringblock(CSTR(_topic)); };
            _end=[this](uint8_t* p){ *p=_qos; };
            _build();
        }
};
class UnsubscribePacket: public Packet {
        string          _topic;
    public:
        UnsubscribePacket(const string& topic): _topic(topic),Packet(UNSUBSCRIBE,1,false) {
            _initId();
            _begin=[this]{ _stringblock(CSTR(_topic)); };
            _build();
        }
};

class PublishPacket: public Packet {
        string          _topic;
        uint8_t         _qos;
        bool            _retain;
        size_t          _length;
        bool            _dup;
        uint8_t         _retries;
    public:
        uint16_t        _givenId=0;
        PublishPacket(const char* topic, uint8_t qos, bool retain, uint8_t* payload, size_t length, bool dup,uint16_t givenId=0);
        ~PublishPacket(){ _outbound.erase(_id); }

        bool     resend();
};

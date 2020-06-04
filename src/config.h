#define ASYNC_MQTT_DEBUG true

#define ASMQ_POLL_RATE      2
// per second - depend on LwIP implementation, may need to change as keepalive is scaled from this value
// e.g 15 seconds = 30 poll "ticks"

#define ASMQ_MAX_RETRIES    3

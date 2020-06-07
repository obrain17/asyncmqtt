#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMQTT.h>

#define WIFI_SSID "iBOX-rep"
#define WIFI_PASSWORD ""

#define MQTT_HOST IPAddress(192, 168, 1,21)
#define MQTT_PORT 1883

#define RC_DELAY  8

AsyncMQTT mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

uint8_t  binaryPayload[200]={0x0,0x1,0x2,0x3};
std::string  topic("blimey");
Ticker T,T2;

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println("Connected to Wi-Fi.");
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(RC_DELAY , connectToWifi);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

#define QOS 0

void onMqttConnect(bool sessionPresent) {
  Serial.printf("Connected to MQTT. session=%d\n",sessionPresent);

  int16_t packetIdSub = mqttClient.subscribe(CSTR(topic), QOS);
  Serial.printf("Subscribing to %s at QoS %d, packetId: %d\n",CSTR(topic),QOS,packetIdSub);

  T.attach(5,[]{
    uint32_t heap=ESP.getFreeHeap();
    Serial.printf("T=%u Publish heap=%u to %s at QoS %d\n",millis(),heap,CSTR(topic),QOS);
    //mqttClient.publish(CSTR(topic), QOS, false, (uint8_t*) &heap, sizeof(uint32_t));
    mqttClient.publish(CSTR(topic), QOS, false, (uint8_t*) "AAAA" ,4);
  });
  /*
  mqttClient.publish(CSTR(topic), QOS, false, binaryPayload,200);

  Serial.print("Publishing at QoS 1, packetId: ");
  uint16_t packetIdPub1 = mqttClient.publish("test/lol", 1, true, (uint8_t*) "test 2",6);
  Serial.println(packetIdPub1);
  
  Serial.print("Publishing at QoS 2, packetId: ");
  uint16_t packetIdPub2 = mqttClient.publish("test/lol", 2, true, (uint8_t*) "test 3",6);
  Serial.println(packetIdPub2);
  */
}

void onMqttDisconnect(int8_t reason) {
  Serial.printf("Disconnected from MQTT. r=%d\n",reason);
  T.detach();
  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(RC_DELAY  , connectToMqtt);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttUnsubscribe(uint16_t packetId) {
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void onMqttMessage(const char* topic, uint8_t* payload, ASMQ_PROPS_t prop, size_t len, size_t index, size_t total) {
    mqttClient.dumphex(payload,len);
    //uint32_t heap;
    //memcpy(&heap,payload,index);
    Serial.printf("topic=%s q=%d d=%d r=%d len=%d i=%d total=%d\n",topic,prop.qos,prop.dup,prop.retain,len,index,total);
}

void onMqttPublish(uint16_t packetId) {
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void setup() {
  Serial.begin(115200);

  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);

  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCleanSession(true);
  mqttClient.setMaxRetries(2);
  connectToWifi();
  T2.attach(1,[]{
    Serial.printf("T=%u heap=%u\n",millis(),ESP.getFreeHeap());
  });
}

void loop() {}

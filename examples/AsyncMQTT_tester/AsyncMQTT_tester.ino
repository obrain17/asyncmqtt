#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMQTT.h>

#define WIFI_SSID "iBOX-rep"
#define WIFI_PASSWORD ""

#define MQTT_HOST IPAddress(192, 168, 1,21)
#define MQTT_PORT 1883

AsyncMQTT mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

uint8_t  binaryPayload[200]={0x0,0x1,0x2,0x3};
std::string  topic("blimey");
Ticker T;

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
  wifiReconnectTimer.once(2, connectToWifi);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

#define QOS 0

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);

  int16_t packetIdSub = mqttClient.subscribe(CSTR(topic), QOS);
  Serial.printf("Subscribing to %s at QoS %d, packetId: %d\n",CSTR(topic),QOS,packetIdSub);

  T.attach(5,[]{
    uint32_t heap=ESP.getFreeHeap();
    Serial.printf("Publish heap=%u to %s at QoS %d\n",heap,CSTR(topic),QOS);
    mqttClient.publish(CSTR(topic), QOS, false, (uint8_t*) &heap,sizeof(uint32_t));
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
void onMqttDisconnect(uint8_t reason) {
  Serial.println("Disconnected from MQTT.");
  T.detach();
  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
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
    //mqttClient.dumphex(payload,len);
    uint32_t heap;
    memcpy(&heap,payload,len);
    Serial.printf("Publish received topic=%s heap=%u q=%d d=%d r=%d len=%d\n",topic,heap,prop.qos,prop.dup,prop,prop.retain,len);
}

void onMqttPublish(uint16_t packetId) {
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();

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
}

void loop() {}

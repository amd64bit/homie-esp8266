#include "BootNormal.hpp"

using namespace HomieInternals;

BootNormal::BootNormal()
: Boot("normal")
, _lastWifiReconnectAttempt(0)
, _lastMqttReconnectAttempt(0)
, _lastSignalSent(0)
, _lastUptimeSent(0)
, _setupFunctionCalled(false)
, _wifiConnectNotified(false)
, _wifiDisconnectNotified(true)
, _mqttConnectNotified(false)
, _mqttDisconnectNotified(true)
, _otaVersion({'\0'})
, _flaggedForOta(false)
, _flaggedForReset(false)
{
}

BootNormal::~BootNormal() {
}

void BootNormal::_wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(Config.get().wifi.ssid, Config.get().wifi.password);
}

void BootNormal::_mqttConnect() {
  const char* host = Config.get().mqtt.server.host;
  unsigned int port = Config.get().mqtt.server.port;
  /* if (Config.get().mqtt.mdns) {
    Logger.log(F("Querying mDNS service "));
    Logger.logln(Config.get().mqtt.mdnsService);

    int n = MDNS.queryService(Config.get().mqtt.mdnsService, "tcp");
    if (n == 0) {
      Logger.logln(F("No services found"));
      return;
    } else {
      Logger.log(F("✔ "));
      Logger.log(String(n));
      Logger.logln(F(" service(s) found, using first"));
      host = MDNS.IP(0);
      port = MDNS.port(0);
    }
  } */

  MqttClient.setServer(host, port, Config.get().mqtt.server.ssl.fingerprint);
  MqttClient.setCallback(std::bind(&BootNormal::_mqttCallback, this, std::placeholders::_1, std::placeholders::_2));
  strcpy(MqttClient.getTopicBuffer(), Config.get().mqtt.baseTopic);
  strcat(MqttClient.getTopicBuffer(), Config.get().deviceId);
  strcat_P(MqttClient.getTopicBuffer(), PSTR("/$online"));

  char clientId[MAX_WIFI_SSID_LENGTH];
  strcpy(clientId, this->_interface->brand);
  strcat_P(clientId, PSTR("-"));
  strcat(clientId, Config.get().deviceId);

  if (MqttClient.connect(clientId, "false", 2, true, Config.get().mqtt.auth, Config.get().mqtt.username, Config.get().mqtt.password)) {
    Logger.logln(F("Connected"));
    this->_mqttSetup();
  } else {
    Logger.logln(F("✖ Cannot connect"));
  }
}

void BootNormal::_mqttSetup() {
  Logger.log(F("Sending initial information... "));

  strcpy(MqttClient.getTopicBuffer(), Config.get().mqtt.baseTopic);
  strcat(MqttClient.getTopicBuffer(), Config.get().deviceId);
  strcat_P(MqttClient.getTopicBuffer(), PSTR("/$nodes"));

  char nodes[MAX_REGISTERED_NODES_COUNT * (MAX_NODE_ID_LENGTH + 1 + MAX_NODE_ID_LENGTH + 1) - 1];
  strcpy_P(nodes, PSTR(""));
  for (int i = 0; i < this->_interface->registeredNodesCount; i++) {
    HomieNode* node = this->_interface->registeredNodes[i];
    strcat(nodes, node->getId());
    strcat_P(nodes, PSTR(":"));
    strcat(nodes, node->getType());
    if (i != this->_interface->registeredNodesCount - 1) strcat_P(nodes, PSTR(","));
  }
  if (!MqttClient.publish(nodes, true)) {
    MqttClient.disconnect();
    Logger.logln(F(" Failed"));
    return;
  }

  strcpy(MqttClient.getTopicBuffer(), Config.get().mqtt.baseTopic);
  strcat(MqttClient.getTopicBuffer(), Config.get().deviceId);
  strcat_P(MqttClient.getTopicBuffer(), PSTR("/$online"));
  if (!MqttClient.publish("true", true)) {
    MqttClient.disconnect();
    Logger.logln(F(" Failed"));
    return;
  }

  strcpy(MqttClient.getTopicBuffer(), Config.get().mqtt.baseTopic);
  strcat(MqttClient.getTopicBuffer(), Config.get().deviceId);
  strcat_P(MqttClient.getTopicBuffer(), PSTR("/$name"));
  if (!MqttClient.publish(Config.get().name, true)) {
    MqttClient.disconnect();
    Logger.logln(F(" Failed"));
    return;
  }

  strcpy(MqttClient.getTopicBuffer(), Config.get().mqtt.baseTopic);
  strcat(MqttClient.getTopicBuffer(), Config.get().deviceId);
  strcat_P(MqttClient.getTopicBuffer(), PSTR("/$localip"));
  IPAddress localIp = WiFi.localIP();
  char localIpStr[15 + 1];
  char localIpPartStr[3 + 1];
  itoa(localIp[0], localIpPartStr, 10);
  strcpy(localIpStr, localIpPartStr);
  strcat_P(localIpStr, PSTR("."));
  itoa(localIp[1], localIpPartStr, 10);
  strcat(localIpStr, localIpPartStr);
  strcat_P(localIpStr, PSTR("."));
  itoa(localIp[2], localIpPartStr, 10);
  strcat(localIpStr, localIpPartStr);
  strcat_P(localIpStr, PSTR("."));
  itoa(localIp[3], localIpPartStr, 10);
  strcat(localIpStr, localIpPartStr);
  if (!MqttClient.publish(localIpStr, true)) {
    MqttClient.disconnect();
    Logger.logln(F(" Failed"));
    return;
  }


  strcpy(MqttClient.getTopicBuffer(), Config.get().mqtt.baseTopic);
  strcat(MqttClient.getTopicBuffer(), Config.get().deviceId);
  strcat_P(MqttClient.getTopicBuffer(), PSTR("/$fwname"));
  if (!MqttClient.publish(this->_interface->firmware.name, true)) {
    MqttClient.disconnect();
    Logger.logln(F(" Failed"));
    return;
  }

  strcpy(MqttClient.getTopicBuffer(), Config.get().mqtt.baseTopic);
  strcat(MqttClient.getTopicBuffer(), Config.get().deviceId);
  strcat_P(MqttClient.getTopicBuffer(), PSTR("/$fwversion"));
  if (!MqttClient.publish(this->_interface->firmware.version, true)) {
    MqttClient.disconnect();
    Logger.logln(F(" Failed"));
    return;
  }

  Logger.logln(F(" OK"));

  strcpy(MqttClient.getTopicBuffer(), Config.get().mqtt.baseTopic);
  strcat(MqttClient.getTopicBuffer(), Config.get().deviceId);
  strcat_P(MqttClient.getTopicBuffer(), PSTR("/+/+/set"));
  Logger.log(F("Subscribing to topics... "));
  if (!MqttClient.subscribe(1)) {
    MqttClient.disconnect();
    Logger.logln(F(" Failed"));
    return;
  }

  strcpy(MqttClient.getTopicBuffer(), Config.get().mqtt.baseTopic);
  strcat(MqttClient.getTopicBuffer(), Config.get().deviceId);
  strcat_P(MqttClient.getTopicBuffer(), PSTR("/$reset"));
  if (!MqttClient.subscribe(1)) {
    MqttClient.disconnect();
    Logger.logln(F(" Failed"));
    return;
  }

  if (Config.get().ota.enabled) {
    strcpy(MqttClient.getTopicBuffer(), Config.get().mqtt.baseTopic);
    strcat(MqttClient.getTopicBuffer(), Config.get().deviceId);
    strcat_P(MqttClient.getTopicBuffer(), PSTR("/$ota"));
    if (!MqttClient.subscribe(1)) {
      MqttClient.disconnect();
      Logger.logln(F(" Failed"));
      return;
    }
  }

  Logger.logln(F(" OK"));
}

void BootNormal::_mqttCallback(char* topic, char* payload) {
  String message = String(payload);
  String unified = String(topic);
  unified.remove(0, strlen(Config.get().mqtt.baseTopic) + strlen(Config.get().deviceId) + 1); // Remove devices/${id}/ --- +1 for /

  // Device properties
  if (Config.get().ota.enabled && unified == "$ota") {
    if (message != this->_interface->firmware.version) {
      Logger.log(F("✴ OTA available (version "));
      Logger.log(message);
      Logger.logln(F(")"));
      if (strlen(payload) + 1 <= MAX_FIRMWARE_VERSION_LENGTH) {
        strcpy(this->_otaVersion, payload);
        this->_flaggedForOta = true;
        Logger.logln(F("Flagged for OTA"));
      } else {
        Logger.logln(F("Version string received is too long"));
      }
    }
    return;
  } else if (unified == "$reset" && message == "true") {
    strcpy(MqttClient.getTopicBuffer(), Config.get().mqtt.baseTopic);
    strcat(MqttClient.getTopicBuffer(), Config.get().deviceId);
    strcat_P(MqttClient.getTopicBuffer(), PSTR("/$reset"));
    MqttClient.publish("false", true);
    this->_flaggedForReset = true;
    Logger.logln(F("Flagged for reset by network"));
    return;
  }

  // Implicit node properties
  unified.remove(unified.length() - 4, 4); // Remove /set
  int separator;
  for (int i = 0; i < unified.length(); i++) {
    if (unified.charAt(i) == '/') {
      separator = i;
      break;
    }
  }
  String node = unified.substring(0, separator);
  String property = unified.substring(separator + 1);

  int homieNodeIndex = -1;
  for (int i = 0; i < this->_interface->registeredNodesCount; i++) {
    HomieNode* homieNode = this->_interface->registeredNodes[i];
    if (node == homieNode->getId()) {
      homieNodeIndex = i;
      break;
    }
  }

  if (homieNodeIndex == -1) {
    Logger.log(F("Node "));
    Logger.log(node);
    Logger.logln(F(" not registered"));
    return;
  }

  HomieNode* homieNode = this->_interface->registeredNodes[homieNodeIndex];

  int homieNodePropertyIndex = -1;
  for (int i = 0; i < homieNode->getSubscriptionsCount(); i++) {
    Subscription subscription = homieNode->getSubscriptions()[i];
    if (property == subscription.property) {
      homieNodePropertyIndex = i;
      break;
    }
  }

  if (homieNodePropertyIndex == -1) {
    Logger.log(F("Node "));
    Logger.log(node);
    Logger.log(F(" not subscribed to "));
    Logger.logln(property);
    return;
  }

  Subscription homieNodeSubscription = homieNode->getSubscriptions()[homieNodePropertyIndex];

  Logger.logln(F("Calling global input handler..."));
  bool handled = this->_interface->globalInputHandler(node, property, message);
  if (handled) return;

  Logger.logln(F("Calling node input handler..."));
  handled = homieNode->getInputHandler()(property, message);
  if (handled) return;

  Logger.logln(F("Calling property input handler..."));
  handled = homieNodeSubscription.inputHandler(message);
  if (!handled){
    Logger.logln(F("No handlers handled the following packet:"));
    Logger.log(F("  • Node ID: "));
    Logger.logln(node);
    Logger.log(F("  • Property: "));
    Logger.logln(property);
    Logger.log(F("  • Value: "));
    Logger.logln(message);
  }
}

void BootNormal::_handleReset() {
  if (this->_interface->reset.enabled) {
    this->_resetDebouncer.update();

    if (this->_resetDebouncer.read() == this->_interface->reset.triggerState) {
      this->_flaggedForReset = true;
      Logger.logln(F("Flagged for reset by pin"));
    }
  }

  if (this->_interface->reset.userFunction()) {
    this->_flaggedForReset = true;
    Logger.logln(F("Flagged for reset by function"));
  }
}

void BootNormal::setup() {
  Boot::setup();

  MqttClient.initMqtt(Config.get().mqtt.server.ssl.enabled);

  if (this->_interface->reset.enabled) {
    pinMode(this->_interface->reset.triggerPin, INPUT_PULLUP);

    this->_resetDebouncer.attach(this->_interface->reset.triggerPin);
    this->_resetDebouncer.interval(this->_interface->reset.triggerTime);
  }

  Config.log();

  if (Config.get().mqtt.server.ssl.enabled) {
    Logger.log(F("SSL enabled: pushing CPU frequency to 160MHz... "));
    if (system_update_cpu_freq(SYS_CPU_160MHZ)) {
      Logger.logln("OK");
    } else {
      Logger.logln("Failure");
      Logger.logln("Rebooting...");
      ESP.restart();
    }
  }
}

void BootNormal::loop() {
  Boot::loop();

  this->_handleReset();

  if (this->_flaggedForReset && this->_interface->reset.able) {
    Logger.logln(F("Device is in a resettable state"));
    Config.erase();
    Logger.logln(F("Configuration erased"));

    Logger.logln(F("Triggering HOMIE_ABOUT_TO_RESET event..."));
    this->_interface->eventHandler(HOMIE_ABOUT_TO_RESET);

    Logger.logln(F("↻ Rebooting into config mode..."));
    ESP.restart();
  }

  if (this->_flaggedForOta && this->_interface->reset.able) {
    Logger.logln(F("Device is in a resettable state"));
    Config.setOtaMode(true, this->_otaVersion);

    Logger.logln(F("↻ Rebooting into OTA mode..."));
    ESP.restart();
  }

  this->_interface->readyToOperate = false;

  if (WiFi.status() != WL_CONNECTED) {
    this->_wifiConnectNotified = false;
    if (!this->_wifiDisconnectNotified) {
      this->_lastWifiReconnectAttempt = 0;
      this->_lastUptimeSent = 0;
      this->_lastSignalSent = 0;
      Logger.logln(F("✖ Wi-Fi disconnected"));
      Logger.logln(F("Triggering HOMIE_WIFI_DISCONNECTED event..."));
      this->_interface->eventHandler(HOMIE_WIFI_DISCONNECTED);
      this->_wifiDisconnectNotified = true;
    }

    unsigned long now = millis();
    if (now - this->_lastWifiReconnectAttempt >= WIFI_RECONNECT_INTERVAL || this->_lastWifiReconnectAttempt == 0) {
      Logger.logln(F("↕ Attempting to connect to Wi-Fi..."));
      this->_lastWifiReconnectAttempt = now;
      if (this->_interface->led.enabled) {
        Blinker.start(LED_WIFI_DELAY);
      }
      this->_wifiConnect();
    }
    return;
  }

  this->_wifiDisconnectNotified = false;
  if (!this->_wifiConnectNotified) {
    Logger.logln(F("✔ Wi-Fi connected"));
    Logger.logln(F("Triggering HOMIE_WIFI_CONNECTED event..."));
    this->_interface->eventHandler(HOMIE_WIFI_CONNECTED);
    this->_wifiConnectNotified = true;
  }

  if (!MqttClient.connected()) {
    this->_mqttConnectNotified = false;
    if (!this->_mqttDisconnectNotified) {
      this->_lastMqttReconnectAttempt = 0;
      this->_lastUptimeSent = 0;
      this->_lastSignalSent = 0;
      Logger.logln(F("✖ MQTT disconnected"));
      Logger.logln(F("Triggering HOMIE_MQTT_DISCONNECTED event..."));
      this->_interface->eventHandler(HOMIE_MQTT_DISCONNECTED);
      this->_mqttDisconnectNotified = true;
    }

    unsigned long now = millis();
    if (now - this->_lastMqttReconnectAttempt >= MQTT_RECONNECT_INTERVAL || this->_lastMqttReconnectAttempt == 0) {
      Logger.logln(F("↕ Attempting to connect to MQTT..."));
      this->_lastMqttReconnectAttempt = now;
      if (this->_interface->led.enabled) {
        Blinker.start(LED_MQTT_DELAY);
      }
      this->_mqttConnect();
    }
    return;
  } else {
    if (this->_interface->led.enabled) {
      Blinker.stop();
    }
  }

  this->_interface->readyToOperate = true;

  this->_mqttDisconnectNotified = false;
  if (!this->_mqttConnectNotified) {
    Logger.logln(F("✔ MQTT ready"));
    Logger.logln(F("Triggering HOMIE_MQTT_CONNECTED event..."));
    this->_interface->eventHandler(HOMIE_MQTT_CONNECTED);
    this->_mqttConnectNotified = true;
  }

  if (!this->_setupFunctionCalled) {
    Logger.logln(F("Calling setup function..."));
    this->_interface->setupFunction();
    this->_setupFunctionCalled = true;
  }

  unsigned long now = millis();
  if (now - this->_lastSignalSent >= SIGNAL_QUALITY_SEND_INTERVAL || this->_lastSignalSent == 0) {
    int32_t rssi = WiFi.RSSI();
    unsigned char quality;
    if (rssi <= -100) {
      quality = 0;
    } else if (rssi >= -50) {
      quality = 100;
    } else {
      quality = 2 * (rssi + 100);
    }

    char qualityStr[3 + 1];
    itoa(quality, qualityStr, 10);

    Logger.log(F("Sending Wi-Fi signal quality ("));
    Logger.log(qualityStr);
    Logger.log(F("%)... "));

    strcpy(MqttClient.getTopicBuffer(), Config.get().mqtt.baseTopic);
    strcat(MqttClient.getTopicBuffer(), Config.get().deviceId);
    strcat_P(MqttClient.getTopicBuffer(), PSTR("/$signal"));

    if (MqttClient.publish(qualityStr, true)) {
      Logger.logln(F(" OK"));
      this->_lastSignalSent = now;
    } else {
      Logger.logln(F(" Failure"));
    }
  }

  if (now - this->_lastUptimeSent >= UPTIME_SEND_INTERVAL || this->_lastUptimeSent == 0) {
    Clock.tick();

    char uptimeStr[10 + 1];
    itoa(Clock.getSeconds(), uptimeStr, 10);

    Logger.log(F("Sending uptime ("));
    Logger.log(String(Clock.getSeconds()));
    Logger.log(F("s)... "));

    strcpy(MqttClient.getTopicBuffer(), Config.get().mqtt.baseTopic);
    strcat(MqttClient.getTopicBuffer(), Config.get().deviceId);
    strcat_P(MqttClient.getTopicBuffer(), PSTR("/$uptime"));

    if (MqttClient.publish(uptimeStr, true)) {
      Logger.logln(F(" OK"));
      this->_lastUptimeSent = now;
    } else {
      Logger.logln(F(" Failure"));
    }
  }

  this->_interface->loopFunction();

  MqttClient.loop();
}

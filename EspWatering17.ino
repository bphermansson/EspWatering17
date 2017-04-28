// EspWifi
#include <ESP8266WiFi.h>
// Mqtt
#include <PubSubClient.h>
// DS18B20
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoOTA.h>

// DS18B20 on Gpio13
#define ONE_WIRE_BUS 13  // DS18B20 pin
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

// For reading Vcc
ADC_MODE(ADC_VCC);

const char* ssid = "NETGEAR83";
const char* password = "..........";
const char* mqtt_server = "192.168.1.79";

// Mqtt topic to publish to
const char* mqtt_pub_topic = "espwatering";

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  setup_wifi();

  // Mqtt
  client.setServer(mqtt_server, 1883);
  // Mqtt callback for incoming messages
  //client.setCallback(callback);


  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  // Read Vcc
  float vcc = ESP.getVcc();
  Serial.print("Vcc Value:"); //Print Message
  Serial.println(vcc);     //Print ADC value
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("Espwatering", "emonpi", "emonpimqtt2016")) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish(mqtt_pub_topic, "hello from EspWatering");
      // ... and resubscribe
      //client.subscribe(mqtt_sub_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void loop() {
  float temp;

  ArduinoOTA.handle();

  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  //int volt = (adcvalue * vPow*10) / 1024.0;   // Multiply by ten -> gives a 'decimal' with an int
  //int battv = volt / (r2 / (r1 + r2));
  
  //Serial.println(battv); 

  // Read Vcc
  float vcc = ESP.getVcc();
  Serial.print("Vcc Value:"); //Print Message
  Serial.println(vcc);     //Print ADC value

  DS18B20.requestTemperatures(); 
  temp = DS18B20.getTempCByIndex(0);
  Serial.print("Temperature: ");
  Serial.println(temp);

  // Convert and publish to Mqtt broker
  dtostrf(temp,5,2,msg);
  
  Serial.print("Publish message: ");
  Serial.println(msg);
  client.publish(mqtt_pub_topic, msg);

  delay(120000);
}

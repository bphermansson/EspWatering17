// EspWifi
#include <ESP8266WiFi.h>
// Mqtt
#include <PubSubClient.h>
// DS18B20
#include <OneWire.h>
#include <DallasTemperature.h>
//#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>

#define SLEEP_DELAY_IN_SECONDS 30

// DS18B20 on Gpio13
#define ONE_WIRE_BUS 13  // DS18B20 pin
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

// Home Assistant server 
// http://192.168.1.142:8123/api/states/binary_sensor.ljusute

const char* server = " 192.168.1.142";  // server's address
int port = 8123;
const char* resource = "/api/states/binary_sensor.ljusute"; // http resource
const unsigned long HTTP_TIMEOUT = 10000;  // max respone time from server
const size_t MAX_CONTENT_SIZE = 512;       // max size of the HTTP response

// The type of data that we want to extract from the page
struct UserData {
  char state[5];
};

/* Spänningsdelare på solcell
100k till jord, 390k till solcell
5 volt in ger 1,02 ut

0,675 på adc - Adc Value:646
adc 0,669 solcell 3,292

1023/3,3 * adcvolt = adcvalue
310 * adcvolt = adcvalue
adcvolt=adcvalue/310

solcell 3,153V
adc 0,641V
adc 613

solcell 1,127V
adc 0,228V
adc 227

Divider delar 4,92

adc value * 5 = Solar cell volt i mV.

*/
int solarVoltAdc = 0;

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
  Serial.begin(9600);
  Serial.println("Booting");
  setup_wifi();

  // Mqtt
  client.setServer(mqtt_server, 1883);
  // Mqtt callback for incoming messages
  //client.setCallback(callback);

  // Read Vcc
  solarVoltAdc = analogRead(A0);
  int solarvolt = solarVoltAdc * 5;

  Serial.print("Adc Value:"); //Print Message
  Serial.println(solarVoltAdc);     //Print ADC value

  // Water level sensor on Gpio5
  //pinMode(5, INPUT_PULLUP);

}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  WiFi.hostname("EspWatering");

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
  // Temp value from DS8B20
  float temp;
  
  // Json object to send via Mqtt
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  // Reconnect to Mqtt server if necessary
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

// Check solar voltage and temperature, then send it via Mqtt
  solarVoltAdc = analogRead(A0);
  Serial.print("Adc Value:"); 
  Serial.println(solarVoltAdc);     //Print ADC value
  int solarvolt = solarVoltAdc * 5;
  Serial.print("Solar cell volt:"); 
  Serial.print(solarvolt); 
  Serial.println("mV"); 
  // Add solar volt value to Json object
  root["solarvolt"] = solarvolt;

  DS18B20.requestTemperatures(); 
  temp = DS18B20.getTempCByIndex(0);  // temp is a float
  Serial.print("Temperature: ");
  Serial.println(temp);

  // Convert and publish to Mqtt broker
  
  // Add temp value to json object
  root["temp"] = temp;

  // Convert json object to char
  root.printTo((char*)msg, root.measureLength() + 1);

  //https://gist.github.com/virgilvox/ffe1cc08a240db9792d3
  Serial.println("Publish to Mqtt.");
  client.publish(mqtt_pub_topic, msg);  // Payload as char

  // Shall the waterpump run?
  // Lets ask the Home Assistant installation if it's been run today
  // If not, run it. 
  // We get Json data from http://192.168.1.142:8123/api/states/binary_sensor.ljusute
  Serial.println("connect to Hass");

  HTTPClient http;
  http.begin("http://192.168.1.142:8123/api/states/binary_sensor.ljusute"); //HTTP
  int httpCode = http.GET();
  if(httpCode > 0) {
       // HTTP header has been send and Server response header has been handled
            Serial.println("[HTTP] GET... code: %d\n");
            Serial.println(httpCode);

            // file found at server
            if(httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                Serial.println(payload);
                StaticJsonBuffer<300> jsonBuffer;
                char json[300];
                payload.toCharArray(json,300);
                JsonObject& root = jsonBuffer.parseObject(json);
                // Test if parsing succeeds.
                if (!root.success()) {
                  Serial.println("parseObject() failed");
                  //return;
                }
                String state = root["state"];
                Serial.println("The Hass switch is: ");
                Serial.println(state);

            }
        } else {
            Serial.println("[HTTP] GET... failed, error: %s\n");
           // Serial.println("http.errorToString(httpCode).c_str())";
        }

        http.end();

  Serial.println("Disconnect Mqtt.");
  client.disconnect();
  Serial.print("Disconnect Wifi.");
  //WiFi.disconnect();
  delay(100);
  Serial.print("Enter deep sleep.");
  
  //ESP.deepSleep(SLEEP_DELAY_IN_SECONDS * 1000000, WAKE_RF_DEFAULT);
  delay(500); // wait for deep sleep to happen
  delay(120000);
  delay(10000); // For debug
}


#include <Arduino.h>

// Measure Vcc internally
ADC_MODE(ADC_VCC);

// EspWifi
#include <ESP8266WiFi.h>
// Mqtt
#include <PubSubClient.h>
// DS18B20
#include <OneWire.h>
#include <DallasTemperature.h>

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>

//#define SLEEP_DELAY_IN_SECONDS 30

// Waterpump
int pumpio = 12;
// Watersensor
int watersensor = 14;
byte waterlevel;

// DS18B20 on Gpio13
#define ONE_WIRE_BUS 13  // DS18B20 pin
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

// Home Assistant server
const char* server = " 192.168.1.142";  // server's address
int port = 8123;
//const unsigned long HTTP_TIMEOUT = 10000;  // max respone time from server
//const size_t MAX_CONTENT_SIZE = 512;       // max size of the HTTP response

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
// If checking the solar cell voltage
// Not used here, the ADC is used to monitor battery voltage instead.
//int solarVoltAdc = 0;


const char* ssid = "NETGEAR83";
const char* password = "..........";
const char* mqtt_server = "192.168.1.79";

// Mqtt topic to publish to
const char* mqtt_pub_topic = "espwatering/monitor";
const char* mqtt_pub_values = "espwatering/values";
const char* mqtt_pub_topicPump = "espwateringPump";
const char* mqtt_pub_pumpstatus = "espwatering/pumprun/state";
const char* mqtt_pub_waterlevel = "espwatering/waterlevel";
const char* mqtt_pub_setswitch = "espwatering/pumprun/state";

// Prototypes
void setup_wifi();
void reconnect();
void measurements();

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];

// Temp value from DS8B20
float temp;
boolean stateBool, error;
int iSlider;

// Json object to send via Mqtt
StaticJsonBuffer<200> jsonBuffer;
JsonObject& root = jsonBuffer.createObject();

const char compile_date[] = __DATE__ " " __TIME__;

void setup() {
  // Configure
  Serial.begin(115200);
  Serial.print("Booting version compiled at ");
  Serial.println(compile_date);

  setup_wifi();
  // Mqtt
  client.setServer(mqtt_server, 1883);
  // Water level sensor on Gpio14
  pinMode(watersensor, INPUT_PULLUP);
  // Water pump on Gpio 13
  pinMode(pumpio, OUTPUT);

  // Real work tasks
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Check Vcc and temp, publish it via mqtt
  measurements();
  checkHass();
  if (error){
    Serial.println("Cant connect to Hass, sleeping before next try");
    ESP.deepSleep(20e8);
  }
  runPump();
  Serial.println("Disconnect Mqtt.");
  client.disconnect();

  //ESP.deepSleep(10e6);  // 10s, 10000000 uS
  ESP.deepSleep(20e8); // 2000s, ca 33,3 minutes. 2000000000uS.Green house
}


void loop() {
  // Nothing to do here, all is done in setup
}

void measurements() {
  uint32_t getVcc;

  for (int x=0;x<10;x++) {
    getVcc = ESP.getVcc();
    delay(10);

  }
  root["batt"] = getVcc;
  Serial.print("Vcc: ");
  Serial.println(getVcc);

  DS18B20.requestTemperatures();
  temp = DS18B20.getTempCByIndex(0);  // temp is a float
  root["temp"] = temp;
  Serial.print("Temperature: ");
  Serial.println(temp);

  waterlevel = !digitalRead(watersensor);
  root["waterlevel"] = waterlevel;
  Serial.print("Waterlevel (0=empty) : ");
  Serial.println(waterlevel);
  // Convert json object to char
  root.printTo((char*)msg, root.measureLength() + 1);
  Serial.println("Publish to Mqtt.");
  client.publish(mqtt_pub_values, msg);  // Payload as char
}

boolean checkHass(){

    // Shall the waterpump run?
    // Lets ask the Home Assistant installation if it's been run today
    // If not, run it.
    // We get Json data from http://192.168.1.142:8123/api/states/switch.pumprun

  Serial.println("Connect to Hass");
  HTTPClient http;
  http.begin("http://192.168.1.142:8123/api/states/switch.pumprun"); //HTTP
  int httpCode;
  httpCode = http.GET();
  if(httpCode > 0) {
      if(httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println("Got data from Home Assistant");
        StaticJsonBuffer<300> jsonBuffer;
        char json[300];
        payload.toCharArray(json,300);
        JsonObject& root = jsonBuffer.parseObject(json);
        // Test if parsing succeeds.
        if (!root.success()) {
          Serial.println("parseObject() failed");
        }
        String state = root["state"];
        if (state=="on") {
          stateBool=true;
        }
        else {
          stateBool=false;
        }
      }
  }
  else {
    Serial.println("[HTTP] GET failed");
    error=true;
    return error;
  }
  http.end();

  http.begin("http://192.168.1.142:8123/api/states/switch.pumprun"); //HTTP
  httpCode = http.GET();
  if(httpCode > 0) {
      if(httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println("Got slider data from Home Assistant");
        StaticJsonBuffer<300> jsonBuffer;
        char json[300];
        payload.toCharArray(json,300);
        JsonObject& root = jsonBuffer.parseObject(json);
        // Test if parsing succeeds.
        if (!root.success()) {
          Serial.println("parseObject() failed");
        }
        String slider = root["state"];
        iSlider = slider.toInt();
      }
  }
  else {
    Serial.println("[HTTP] GET failed");
    error=true;
    return error;
  }
  http.end();

  Serial.println("The Hass switch is: ");
  Serial.println(stateBool);
}

void runPump(){
    // State is set to off at sunrise and to on when the pump runs
    if (!stateBool) {
      // Pump has not run today
      if (waterlevel == 0) {
        Serial.println("Water level ok");
        client.publish(mqtt_pub_topic, "Water level ok");
        Serial.println ("Start pump");
        digitalWrite(pumpio, HIGH);
        // Tell Hass about the run
        client.publish(mqtt_pub_setswitch, "ON");  //Set the switch high
        client.publish(mqtt_pub_topic, "Pump started");
        delay(iSlider);  // Run for x seconds
        digitalWrite(pumpio, LOW);
        Serial.println ("Stop pump");
        client.publish(mqtt_pub_topic, "Pump stopped");
      }
      // Water tank empty
     else {
        Serial.println("Water tank empty");
        client.publish(mqtt_pub_topic, "Out of water!");
        //client.publish(mqtt_pub_waterlevel, "LOW");
     }
   }
   else {
     Serial.println("Not running pump");
   }
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
      client.publish(mqtt_pub_topic, "Hello from EspWatering");
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

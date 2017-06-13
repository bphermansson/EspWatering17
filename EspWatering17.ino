// EspWifi
#include <ESP8266WiFi.h>
// Mqtt
#include <PubSubClient.h>
// DS18B20
#include <OneWire.h>
#include <DallasTemperature.h>
//#include <ArduinoOTA.h>
#include <ArduinoJson.h>

#define SLEEP_DELAY_IN_SECONDS 30

// DS18B20 on Gpio13
#define ONE_WIRE_BUS 13  // DS18B20 pin
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

// Home Assistant server 
// http://192.168.1.142:8123/api/states/binary_sensor.ljusute

const char* server = " http://192.168.1.142";  // server's address
const int port = 8123;
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
  if (connect(server)) {
    if (sendRequest(server, resource) && skipResponseHeaders()) {
      UserData userData;
      if (readReponseContent(&userData)) {
        printUserData(&userData);
      }
    }
  }
  disconnect();
  

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

// Open connection to the HTTP server
bool connect(const char* hostName) {
  Serial.print("Connect to ");
  Serial.println(hostName);
  bool ok = espClient.connect(hostName, port);
  Serial.println(ok ? "Connected" : "Connection Failed!");
  return ok;
}

// Send the HTTP GET request to the server
bool sendRequest(const char* host, const char* resource) {
  Serial.print("GET ");
  Serial.println(resource);

  espClient.print("GET ");
  espClient.print(resource);
  espClient.println(" HTTP/1.0");
  espClient.print("Host: ");
  espClient.println(host);
  espClient.println("Connection: close");
  espClient.println();

  return true;
}

// Skip HTTP headers so that we are at the beginning of the response's body
bool skipResponseHeaders() {
  // HTTP headers end with an empty line
  char endOfHeaders[] = "\r\n\r\n";

  espClient.setTimeout(HTTP_TIMEOUT);
  bool ok = espClient.find(endOfHeaders);

  if (!ok) {
    Serial.println("No response or invalid response!");
  }

  return ok;
}
bool readReponseContent(struct UserData* userData) {
  // Compute optimal size of the JSON buffer according to what we need to parse.
  // This is only required if you use StaticJsonBuffer.
  const size_t BUFFER_SIZE =
      JSON_OBJECT_SIZE(8)    // the root object has 8 elements
      + JSON_OBJECT_SIZE(5)  // the "address" object has 5 elements
      + JSON_OBJECT_SIZE(2)  // the "geo" object has 2 elements
      + JSON_OBJECT_SIZE(3)  // the "company" object has 3 elements
      + MAX_CONTENT_SIZE;    // additional space for strings

  // Allocate a temporary memory pool
  DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);

  JsonObject& root = jsonBuffer.parseObject(client);

  if (!root.success()) {
    Serial.println("JSON parsing failed!");
    return false;
  }

  // Here were copy the strings we're interested in
  strcpy(userData->state, root["state"]);
  // It's not mandatory to make a copy, you could just use the pointers
  // Since, they are pointing inside the "content" buffer, so you need to make
  // sure it's still in memory when you read the string

  return true;
}

// Print the data extracted from the JSON
void printUserData(const struct UserData* userData) {
  Serial.print("State = ");
  Serial.println(userData->state);
}

// Close the connection with the HTTP server
void disconnect() {
  Serial.println("Disconnect");
  espClient.stop();
}


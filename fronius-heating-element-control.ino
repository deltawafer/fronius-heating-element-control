#include <Arduino_JSON.h>
#include <ESP8266HTTPClient.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// User parameters
#define WW_HEATER_P1_POWER 1875     // Watts
#define WW_HEATER_P2_POWER 1875     // Watts
#define WW_HEATER_P3_POWER 1875     // Watts
#define UPDATE_CYCLE_TIME 5         // Seconds
#define MAX_AKKU_DISCHARGE 200      // Watts
#define GRID_FEED_THRESHOLD 2000    // Watts
#define MIN_GRID_FEED_THRESHOLD 100 // Watts
#define MIN_REMAIN_TIME 20          // Seconds

// Ouput pin
#define P1_OUT 15
#define P2_OUT 13
#define P3_OUT 12

// API URL
const char* URL_Powerflow = "http://INVERTERIPADDRESS/solar_api/v1/GetPowerFlowRealtimeData.fcgi";
const char* URL_Meter = "http://INVERTERIPADDRESS/solar_api/v1/GetMeterRealtimeData.cgi";

// Wifi access
const char* ssid = "WIFISSID";
const char* password = "WIFIPASSWORD";

// MQTT Broker
const char* mqtt_broker = "MQTTBROKERIP";
const int mqtt_port = 1883;
const char* topic = "solar/heating";
const char* topic_subscribe = "solar/heating/control";

// Initialize Wifi and PubSub
WiFiClient espClient;
PubSubClient client(espClient);

// Working varibles
int lastTime = 0;

  // Inverter values
double gridpower = 0;
double akkupower = 0;
double loadpower = 0;
double solarpower = 0;

double p1gridpower = 0;
double p2gridpower = 0;
double p3gridpower = 0;

double p1loadpower = 0;
double p2loadpower = 0;
double p3loadpower = 0;

  // heating element

    // warmwater real states
bool ww_p1_real_state = 0;
bool ww_p2_real_state = 0;
bool ww_p3_real_state = 0;
    // warmwater potential states
bool ww_p1_potential_state = 0;
bool ww_p2_potential_state = 0;
bool ww_p3_potential_state = 0;

////////////////////////////////////////////////////////////
// Setup
////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(9600);

  // Setup pins
  pinMode(P1_OUT, OUTPUT);
  pinMode(P2_OUT, OUTPUT);
  pinMode(P3_OUT, OUTPUT);

  // WiFi Setup
  setupWifi();

  // MQTT Client Setup
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(callback);
  while (!client.connected()) {
      String client_id = "esp8266-client-heizstab-";
      client_id += String(WiFi.macAddress());
      Serial.printf("The client %s connects to the mqtt broker\n", client_id.c_str());
      if (client.connect(client_id.c_str())) {
      } else {
          Serial.print("failed with state ");
          Serial.print(client.state());
          delay(2000);
      }
  }
  client.subscribe(topic_subscribe);
  Serial.println("MQTT Connection successful");
}

////////////////////////////////////////////////////////////
// Loop
////////////////////////////////////////////////////////////

void loop() {
  // MQTT client loop
  client.loop();

  // Update pins
  updatePinOutput();

  // Controllogic cycle
  if ((millis() - lastTime) > UPDATE_CYCLE_TIME*1000) {
    //Check WiFi connection status
    if(WiFi.status()== WL_CONNECTED){

      // Update power values
      fetchInverterData();

      printInverterData();

      // Check if phases are on
      ww_p1_real_state = (p1loadpower > WW_HEATER_P1_POWER) && (ww_p1_potential_state == 1);
      ww_p2_real_state = (p2loadpower > WW_HEATER_P2_POWER) && (ww_p2_potential_state == 1);
      ww_p3_real_state = (p3loadpower > WW_HEATER_P3_POWER) && (ww_p3_potential_state == 1);
      Serial.print("WW HEATER REAL STATE P1/P2/P3: ");
      Serial.print(ww_p1_real_state);
      Serial.print(ww_p2_real_state);
      Serial.println(ww_p3_real_state);

      // Check for potential states
        // If akku is discharging, no heating allowed (positive akkupower = discharging)
      if (akkupower > MAX_AKKU_DISCHARGE) {
        ww_p1_potential_state = 0;
        ww_p2_potential_state = 0;
        ww_p3_potential_state = 0;
      } else {
        // If gridpower feed-in is sufficient, step up activated phases (negative gridpower = feed-in)
        if (gridpower < -GRID_FEED_THRESHOLD) {
          if ((ww_p1_potential_state == 0) && (ww_p2_potential_state == 0) && (ww_p3_potential_state == 0)) {

              ww_p1_potential_state = 1;
              ww_p2_potential_state = 0;
              ww_p3_potential_state = 0;

          } else if ((ww_p1_potential_state == 1) && (ww_p2_potential_state == 0) && (ww_p3_potential_state == 0)) {
            if (ww_p1_real_state == 1) {
              ww_p1_potential_state = 1;
              ww_p2_potential_state = 1;
              ww_p3_potential_state = 0;
            }
          } else if ((ww_p1_potential_state == 1) && (ww_p2_potential_state == 1) && (ww_p3_potential_state == 0)) {
            if (ww_p2_real_state == 1) {
              ww_p1_potential_state = 1;
              ww_p2_potential_state = 1;
              ww_p3_potential_state = 1;
            }
          }
        // If gridpower is not sufficient (anymore), step down activated phases
        } else if (gridpower > -MIN_GRID_FEED_THRESHOLD) {
          if ((ww_p1_potential_state == 1) && (ww_p2_potential_state == 1) && (ww_p3_potential_state == 1)) {
            ww_p1_potential_state = 1;
            ww_p2_potential_state = 1;
            ww_p3_potential_state = 0;
          } else if ((ww_p1_potential_state == 1) && (ww_p2_potential_state == 1) && (ww_p3_potential_state == 0)) {
            ww_p1_potential_state = 1;
            ww_p2_potential_state = 0;
            ww_p3_potential_state = 0;
          } else if ((ww_p1_potential_state == 1) && (ww_p2_potential_state == 0) && (ww_p3_potential_state == 0)) {
            ww_p1_potential_state = 0;
            ww_p2_potential_state = 0;
            ww_p3_potential_state = 0;
          }
        }
      }

      Serial.print("WW HEATER POTENTIAL STATE P1/P2/P3: ");
      Serial.print(ww_p1_potential_state);
      Serial.print(ww_p2_potential_state);
      Serial.println(ww_p3_potential_state);
      
    }
    else {
      Serial.println("WARNING: WiFi Disconnected, switching heater off");
      ww_p1_potential_state = 0;
      ww_p2_potential_state = 0;
      ww_p3_potential_state = 0;
      setupWifi();
    }
    lastTime = millis();
  }
}


////////////////////////////////////////////////////////////
// Functions
////////////////////////////////////////////////////////////

void updatePinOutput() {
  static int last_ww_heater_state = 0;
  static int last_change_time = 0;
  if ((millis() - last_change_time) > MIN_REMAIN_TIME*1000) {
    int ww_heater_state = (ww_p1_potential_state << 2) && (ww_p2_potential_state << 1) && (ww_p3_potential_state);

    digitalWrite(P1_OUT, ww_p1_potential_state); 
    digitalWrite(P2_OUT, ww_p2_potential_state); 
    digitalWrite(P3_OUT, ww_p3_potential_state); 

    if (ww_heater_state != last_ww_heater_state) {
      last_change_time = millis();
    }
    last_ww_heater_state = ww_heater_state;
  }
}

void sendMQTTStatusupdate() {
      JSONVar statusupdate;
      String statusupdate_string = "ERROR";

      statusupdate["sensor"] = "heizstab";
      statusupdate["time"] = millis();
      statusupdate["ww_p1_potential_state"] = ww_p1_potential_state;
      statusupdate["ww_p2_potential_state"] = ww_p2_potential_state;
      statusupdate["ww_p3_potential_state"] = ww_p3_potential_state;
      statusupdate["ww_p1_real_state"] = ww_p1_real_state;
      statusupdate["ww_p2_real_state"] = ww_p2_real_state;
      statusupdate["ww_p3_real_state"] = ww_p3_real_state;
      statusupdate_string = JSON.stringify(statusupdate);
      client.publish(topic, statusupdate_string.c_str());
      Serial.print("MQTT Statusupdate: ");
      Serial.println(statusupdate_string);
}

void callback(char *topic, byte *payload, unsigned int length) {
    Serial.print("Message arrived in topic: ");
    Serial.println(topic);
    Serial.print("Message:");
    for (int i = 0; i < length; i++) {
        Serial.print((char) payload[i]);
    }
    Serial.println();
    Serial.println("-----------------------");
}

void fetchInverterData() {
  // This function updates the inveter data by http request
  String PowerflowData;
  String MeterData;

  // Send GET request to inverter
  PowerflowData = httpGETRequest(URL_Powerflow);
  MeterData = httpGETRequest(URL_Meter);

  // Parse payload to JSON
  JSONVar PowerflowJsonObject = JSON.parse(PowerflowData);
  JSONVar MeterJsonObject = JSON.parse(MeterData);

  // Check if parsing failed
  if (JSON.typeof(PowerflowJsonObject) == "undefined") {
    Serial.println("Parsing Powerflow input failed!");
    return;
  }
  if (JSON.typeof(MeterJsonObject) == "undefined") {
    Serial.println("Parsing Meter input failed!");
    return;
  }

  // Update variables
  akkupower = double(PowerflowJsonObject["Body"]["Data"]["Site"]["P_Akku"]);
  gridpower = double(PowerflowJsonObject["Body"]["Data"]["Site"]["P_Grid"]);
  loadpower = double(PowerflowJsonObject["Body"]["Data"]["Site"]["P_Load"]);
  solarpower = double(PowerflowJsonObject["Body"]["Data"]["Site"]["P_PV"]);

  p1gridpower = double(MeterJsonObject["Body"]["Data"]["0"]["PowerReal_P_Phase_1"]);
  p2gridpower = double(MeterJsonObject["Body"]["Data"]["0"]["PowerReal_P_Phase_2"]);
  p3gridpower = double(MeterJsonObject["Body"]["Data"]["0"]["PowerReal_P_Phase_3"]);

  p1loadpower = (solarpower/3) - (-p1gridpower) - (-akkupower/3);
  p2loadpower = (solarpower/3) - (-p2gridpower) - (-akkupower/3);
  p3loadpower = (solarpower/3) - (-p3gridpower) - (-akkupower/3);
}

String httpGETRequest(const char* serverName) {
  WiFiClient client;
  HTTPClient http;

  http.begin(client, serverName);
  
  int httpResponseCode = http.GET();
  
  String payload = "{}"; 
  
  if (httpResponseCode>0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }

  http.end();

  return payload;
}


void setupWifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  Serial.print("Wifi connected: ");
  Serial.println(WiFi.localIP());
}

void printInverterData() {
  Serial.print("Akku_Power = ");
  Serial.print(akkupower);
  Serial.print(" / Grid_Power = ");
  Serial.print(gridpower);
  Serial.print(" / Load_Power = ");
  Serial.print(loadpower);
  Serial.print(" / Solar_Power = ");

  Serial.println(solarpower);
  Serial.print("P1Grid_Power = ");
  Serial.print(p1gridpower);
  Serial.print(" / P2Grid_Power = ");
  Serial.print(p2gridpower);
  Serial.print(" / P3Grid_Power = ");
  Serial.println(p3gridpower);

  Serial.print("P1Load_Power = ");
  Serial.print(p1loadpower);
  Serial.print(" / P2Load_Power = ");
  Serial.print(p2loadpower);
  Serial.print(" / P3Load_Power = ");
  Serial.println(p3loadpower);
}
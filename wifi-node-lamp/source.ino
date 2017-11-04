#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include "RTClib.h"
#include <DHT.h>

// RTC
RTC_DS1307 rtc;
// DHT sensor
const short DHTPin = 14;
#define DHTTYPE DHT22
// Initialize DHT sensor
DHT dht(DHTPin, DHTTYPE);

// Information about wifi router
const char* ssid = "####";
const char* password = "####";

// Information about MQTT server
const char* mqtt_server = "###.###.###.###";
const char* mqtt_user = "###";
const char* mqtt_pass = "###";

// Initializes the espClient
WiFiClient espClient;
PubSubClient client(espClient);

// GPIO Pin
const short GPIO13 = 13; // Auto Lamp in L
const short GPIO12 = 12; // Outside Lamp in L

bool status_outlamp = false;
bool status_serlamp = false;

// Default ontime at 18:30 and offtime at 5:00
unsigned short autoLampOnTime = 1110;
unsigned short autoLampOffTime = 300;

// Timers auxiliar variables
long now = millis();
long lastMeasure = 0;

// Setup wifi connection
void setup_wifi() 
{
  delay(10);
  // We start by connecting to a WiFi network
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
  }
}

// Get information about channels subscribed
void callback(String topic, byte* message, unsigned int length) 
{
  String messageTemp;
  
  for (int i = 0; i < length; i++) 
  {
    messageTemp += (char)message[i];
  }

  if (topic == "home/seclamp/set_ontime")
  {
    autoLampOnTime = messageTemp.toInt();
  }
  else if (topic == "home/seclamp/set_offtime")
  {
    autoLampOffTime = messageTemp.toInt();
  }
  else if (topic == "home/outlamp/switch")
  {
    if (messageTemp == "on")
    {
      digitalWrite(GPIO12, LOW);
      status_outlamp = true;
    }
    else if (messageTemp == "off")
    {
      digitalWrite(GPIO12, HIGH);
      status_outlamp = false;
    }
  }
  else if (topic == "home/set_realtime")
  {
    unsigned int timeunix = messageTemp.toInt();
    rtc.adjust(DateTime(timeunix));
  }
}

// This functions reconnects your ESP8266 to your MQTT broker
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) 
  {
    //Attempting MQTT connection...
    if (client.connect("ESP8266Client", mqtt_user, mqtt_pass)) 
    {
      // Connected to MQTT broker
      // Subscribe or resubscribe to a topic
      client.subscribe("home/seclamp/set_ontime");
      client.subscribe("home/seclamp/set_offtime");
      client.subscribe("home/outlamp/switch");
      client.subscribe("home/set_realtime");
    } 
    else 
    {
      // Failed
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() 
{
  pinMode(GPIO13, OUTPUT);
  pinMode(GPIO12, OUTPUT);

  //Serial.begin(115200);
  setup_wifi();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
  if (! rtc.begin()) 
  {
    // Couldn't find RTC
    while (1);
  }

  if (! rtc.isrunning()) 
  {
    // RTC is NOT running!
    // following line sets the RTC to the date & time this sketch was compiled
    // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }

  delay(1000);
  digitalWrite(GPIO12, HIGH);
  digitalWrite(GPIO13, HIGH);
}

DateTime nowTime;
unsigned int currTime = 0;

void loop() {
  if (!client.connected()) 
  {
    reconnect();
  }
  if(!client.loop()) 
  {
    client.connect("ESP8266Client");
  }

  nowTime = rtc.now();
  now = millis();

  // Publishes new time on node mcu every 10 seconds
  if (now - lastMeasure > 10000) 
  {
    lastMeasure = now;
   
    currTime = nowTime.hour()*60 + nowTime.minute();
    if ((currTime >= autoLampOnTime && currTime <= 1439) || (currTime >= 0 && currTime <= autoLampOffTime))
    {
      digitalWrite(GPIO13, LOW);
      status_serlamp = true;
    }
    else
    {
      digitalWrite(GPIO13, HIGH);
      status_serlamp = false;
    }
    
    static char buffer[19];
    sprintf(buffer, "%02d:%02d:%02d %02d/%02d/%d", nowTime.hour(), nowTime.minute(), nowTime.second(), nowTime.day(), nowTime.month(), nowTime.year());
    client.publish("home/realtime", buffer);
    
    static char time_[5];
    sprintf(time_, "%d", autoLampOnTime);
    client.publish("home/seclamp/get_ontime", time_);
    sprintf(time_, "%d", autoLampOffTime);
    client.publish("home/seclamp/get_offtime", time_);

    if (status_outlamp) client.publish("home/outlamp/status", "ON");
    else client.publish("home/outlamp/status", "OFF");

    static char localip[16];
    sprintf(localip, "%d.%d.%d.%d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
    client.publish("home/ip", localip);

    float h = dht.readHumidity();
    float t = dht.readTemperature();
    float f = dht.readTemperature(true);
    if (isnan(h) || isnan(t) || isnan(f)) {
      client.publish("home/outroom/real_temp", "Failed to connect to DHT");
      client.publish("home/outroom/temp", "Failed to connect to DHT");
      client.publish("home/outroom/humidity", "Failed to connect to DHT");
    }
    else
    {
      // Computes temperature values in Celsiu
      float hic = dht.computeHeatIndex(t, h, false);
      static char temperatureTemp[7];
      dtostrf(hic, 6, 2, temperatureTemp);

      static char humidityTemp[7];
      dtostrf(h, 6, 2, humidityTemp);

      // Publishes Temperature and Humidity values
      client.publish("home/outroom/temp", temperatureTemp);
      dtostrf(t, 6, 2, temperatureTemp);
      client.publish("home/outroom/real_temp", temperatureTemp);
      client.publish("home/outroom/humidity", humidityTemp);
    }
  }
}

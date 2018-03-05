#include <ESP8266WiFi.h>  //For ESP8266
#include <ESP8266mDNS.h>  //For OTA
#include <WiFiUdp.h>      //For OTA
#include <ArduinoOTA.h>   //For OTA
#include <PubSubClient.h> //For MQTT
#include <RTClib.h>       // For Realtime
#include <DHT.h>          // For Sensor DHT22

#include "config.h"

uint32_t char2UL(const char *str)
{
  uint32_t result = 0;

  for (int i = 0; str[i] != '\0'; ++i)
  {
    if (!isDigit(str[i])) return 0;
    result = result*10 + str[i] - '0';
  }
  return result;
}

// RTC
RTC_DS1307 rtc;
// DHT sensor
const short DHTPin = 14;
#define DHTTYPE DHT22
// Initialize DHT sensor
DHT dht(DHTPin, DHTTYPE);

// Initializes the espClient
WiFiClient espClient;
PubSubClient client(espClient);

//Necesary to make Arduino Software autodetect OTA device
WiFiServer TelnetServer(8266);

// GPIO Pin
const short GPIO_SEC = 13; // Sec Lamp in L
const short GPIO_OUT = 12; // Outside Lamp in L

// Status
bool status_outlamp = false;
bool status_serlamp = false;

bool timer_serlamp = true;

// Default ontime at 18:30 and offtime at 5:00
unsigned short secLampOnTime = 1110;
unsigned short secLampOffTime = 300;

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
    secLampOnTime = messageTemp.toInt();
  }
  else if (topic == "home/seclamp/set_offtime")
  {
    secLampOffTime = messageTemp.toInt();
  }
  else if (topic == "home/outlamp/switch")
  {
    if (messageTemp == "on")
    {
      digitalWrite(GPIO_OUT, LOW);
      status_outlamp = true;
    }
    else if (messageTemp == "off")
    {
      digitalWrite(GPIO_OUT, HIGH);
      status_outlamp = false;
    }
  }
  else if (topic == "home/seclamp/switch_timer") 
  {
    if (messageTemp == "on")
    {
      timer_serlamp = true;
    }
    else if (messageTemp == "off")
    {
      timer_serlamp = false;
    }
  }
  else if (topic == "home/seclamp/switch")
  {
    if (messageTemp == "on")
    {
      digitalWrite(GPIO_SEC, LOW);
      timer_serlamp = false;
    }
    else if (messageTemp == "off")
    {
      digitalWrite(GPIO_SEC, HIGH);
      timer_serlamp = false;
    }
  }
  else if (topic == "home/set_realtime")
  {
    uint32_t timeunix = char2UL(messageTemp.c_str());
    
    if (timeunix != 0) 
    {
      rtc.adjust(DateTime(timeunix));
    }
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

      client.subscribe("home/seclamp/switch");
      client.subscribe("home/seclamp/switch_timer");

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
  pinMode(GPIO_SEC, OUTPUT);
  pinMode(GPIO_OUT, OUTPUT);

  setup_wifi();

  ota_hostName += ESP.getChipId();
  ArduinoOTA.setHostname(ota_hostName.c_str());
  ArduinoOTA.setPassword(ota_passWord);

  TelnetServer.begin();   //Necesary to make Arduino Software autodetect OTA device  
  ArduinoOTA.begin();

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
  digitalWrite(GPIO_OUT, HIGH);
  digitalWrite(GPIO_SEC, HIGH);
}

DateTime nowTime;
unsigned int currTime = 0;

void loop() 
{
  ArduinoOTA.handle();

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

    if (timer_serlamp == true) 
    {
      if ((currTime >= secLampOnTime && currTime <= 1439) || (currTime >= 0 && currTime <= secLampOffTime))
      {
        digitalWrite(GPIO_SEC, LOW);
        status_serlamp = true;
      }
      else
      {
        digitalWrite(GPIO_SEC, HIGH);
        status_serlamp = false;
      }
    }
    
    static char buffer[19];
    sprintf(buffer, "%02d:%02d:%02d %02d/%02d/%d", nowTime.hour(), nowTime.minute(), nowTime.second(), nowTime.day(), nowTime.month(), nowTime.year());
    client.publish("home/realtime", buffer);
    
    static char time_[5];
    sprintf(time_, "%d", secLampOnTime);
    client.publish("home/seclamp/get_ontime", time_);
    sprintf(time_, "%d", secLampOffTime);
    client.publish("home/seclamp/get_offtime", time_);

    if (status_outlamp) client.publish("home/outlamp/status", "ON");
    else client.publish("home/outlamp/status", "OFF");

    if (status_serlamp) client.publish("home/seclamp/status", "ON");
    else client.publish("home/seclamp/status", "OFF");

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
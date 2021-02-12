


// M5Stack Home Assistant RFID MQTT alarm panel
// Author: Remco Hannink
// Version: 0.3
// Date: 30-04-2020

#include "FS.h"
#include "SPIFFS.h"
#include "M5Stack.h"
#include <M5ez.h>
#include <ezTime.h>
#include <Wire.h>
#include "MFRC522_I2C.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>

// set MQTT timeout parameters to overcome constant reconnection

#define MQTT_KEEPALIVE 60

// The parameters below should be adapted to your own situation

const char ssid[] = "mywlan";
const char pass[] = "secret";
const char* mqtt_host = "host/ip";
const int mqtt_port = 1883;
const char* mqtt_user = "m_user;
const char* mqtt_pass = "m_pwd";
const char* mqtt_clientId = "M5stack-door";
const char* mqtt_ack_topic = "m5_door/ack";
const char* mqtt_battery_stats_topic = "m5_door/stats/battery";
const uint16_t deep_sleep_delay_ms = 60 * 1000; // goto deep sleep after 1 minute

WiFiClient m5stackClient;
PubSubClient client(m5stackClient);
long lastMsg = 0;
char msg[50];
char buttonString[50];
String alarmStatus ="";
byte readCard[4];

int beepVolume=1;
int screenTimeout = 10000; // screen timeout in mSec
int screenStatus = 1;

unsigned long lastStatMillis=0;

MFRC522 mfrc522(0x28); 

void setup(){
    #include <themes/default.h>

    M5.begin(true, false, true, true);

    // turn off the M5stack amplifier to overcome I2C interference with speaker. 
    // For modification see: http://community.m5stack.com/topic/367/mod-to-programmatically-disable-speaker-whine-hiss
    
    pinMode(5,OUTPUT);
    digitalWrite(5, LOW);

    // Mount SPIFFS for icons and pictures
    if(!SPIFFS.begin(true)){
        Serial.println("SPIFFS Mount Failed");
        return;
    }

    // initialize ez5stack screen and buttons
    ez.begin();
    ez.canvas.clear();
    M5.Lcd.fillScreen(TFT_WHITE);
    M5.Lcd.drawJpgFile(SPIFFS, "/hourglass.jpg", 80, 40);
    ez.header.show("Verbinde mit Wifi...");

    // Setup Wifi
    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
 n       ez.header.show("Verbinde mit Homeassistant...");

    // Initialize RFID reader
    mfrc522.PCD_Init();             // Init MFRC522

    client.setServer(mqtt_host, mqtt_port);
    client.setCallback(callback);
    reconnect();
    
    postBatteryStats();
}

void callback(char* topic, byte* payload, unsigned int length) {
  String payloadStr= "";
  M5.Lcd.setBrightness(50); 

  for (int i = 0; i < length; i++) {
    payloadStr +=((char)payload[i]);
  }
  Serial.print("Received: ");
  Serial.println(payloadStr);

    M5.Lcd.fillScreen(TFT_WHITE);
    M5.Lcd.drawJpgFile(SPIFFS, "/blue.jpg", 80, 40);
    ez.header.show("wird geöffnet...");
    delay(5000);
    M5.Lcd.fillScreen(TFT_WHITE);
    M5.Lcd.drawJpgFile(SPIFFS, "/card.jpg", 80, 40);
    ez.header.show("Karte bitte");
}

void postBatteryStats()
{
  Serial.println("called");
  const char level=m5.Power.getBatteryLevel();
  Serial.print("Battery level: ");
  Serial.print(level);
  Serial.println(" %");
  if (client.connected())
  {
    Serial.println("publishing stats...");
    char levelStr[4];
    sprintf(levelStr,"%u",level);
    client.publish(mqtt_battery_stats_topic, levelStr);
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
   if (client.connect(mqtt_clientId,mqtt_user,mqtt_pass)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("homeassistant/tag/m5_door/config", "{\"topic\":\"m5_door/tag_scanned\", \"value_template\":\"{{value_json.UID}}\"}");
      // ... and resubscribe
      client.subscribe(mqtt_ack_topic);
      M5.Lcd.fillScreen(TFT_WHITE);
      M5.Lcd.drawJpgFile(SPIFFS, "/card.jpg", 80, 40);
      ez.header.show("Karte bitte");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop(){
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long currentMillis=millis();
  if (currentMillis-lastStatMillis>deep_sleep_delay_ms)
  {
    Serial.println("entering deep sleep mode.");
    m5.powerOFF();
    //m5.Power.deepSleep();
    lastStatMillis=millis();
  }

  M5.update();
  screenTimeout = screenTimeout-200;

  // If screenTimeout is zero then reset timer
  
  if (screenTimeout == 0){
    screenTimeout=10000;
    
    // if screenTimeout is zero and screenStatus is on, the dim the screen
    
    if (screenStatus == 1){
  
      // if timer is zero and screen is on, dim the screen graduately in 50 steps ans set screen Status to 0
      
      for (int i = 50; i >= 0; i--) {
        M5.Lcd.setBrightness(i);
        delay(20);
      }
      screenStatus = 0;
    }
  }
  
  // If any button was pressed while screen is off, the just turn screen on and do nothing else
  
  if(M5.BtnA.wasPressed() && screenStatus == 0){
    M5.Lcd.setBrightness(50);
    screenTimeout=10000;
    screenStatus = 1;
    delay(200);
  }
  
  // Look for new cards, and select one if present
  
  if ( ! mfrc522.PICC_IsNewCardPresent() || ! mfrc522.PICC_ReadCardSerial() ) {
    delay(200);
    return;
  }
  
  // Now a card is read. The UID is in mfrc522.uid.
  // sound beep and turn on screen;

  M5.Lcd.setBrightness(50);
  screenTimeout = 10000;
  screenStatus = 1;
  beep(100);  

    char uidStr[9]="";
    array_to_string(mfrc522.uid.uidByte, 4, uidStr);
    uidStr[8]='\0';
    Serial.print(F("card detected. UID: "));
    Serial.println(uidStr);

    char tagMessage[19];
    sprintf(tagMessage, "{\"UID\":\"%s\"}", uidStr);
    client.publish("m5_door/tag_scanned", tagMessage);
}

uint8_t music[1000];
void tone_volume(uint16_t frequency, uint32_t duration) {
  float interval=0.001257 * float(frequency);
  float phase=0;
  for (int i=0;i<1000;i++) {
    music[i]=127+126 * sin(phase);
    phase+=interval;
  }
  music[999]=0;
  int remains=duration;
  for (int i=0;i<duration;i+=200) {
    if (remains<200) {
      music[remains * 999/200]=0;
    }
    M5.Speaker.playMusic(music,5000);
    remains-=200;
  }
}

void beep(int leng) {
  digitalWrite(5, HIGH);
  delay(150);
  M5.Speaker.setVolume(1);
  M5.Speaker.update();
  tone_volume(1000, leng);
  digitalWrite(5, LOW);
}
  
void array_to_string(byte array[], unsigned int len, char buffer[])
{
    for (unsigned int i = 0; i < len; i++)
    {
        byte nib1 = (array[i] >> 4) & 0x0F;
        byte nib2 = (array[i] >> 0) & 0x0F;
        buffer[i*2+0] = nib1  < 0xA ? '0' + nib1  : 'A' + nib1  - 0xA;
        buffer[i*2+1] = nib2  < 0xA ? '0' + nib2  : 'A' + nib2  - 0xA;
    }
}

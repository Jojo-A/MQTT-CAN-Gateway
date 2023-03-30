#define USE_LittleFS

#include <FS.h>
#ifdef USE_LittleFS
  #define SPIFFS LittleFS
  #include <LittleFS.h> 
#else
  #include <SPIFFS.h>
#endif

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h> //https://github.com/bblanchon/ArduinoJson

#include "mcp_can.h"
#include <SPI.h>


//#define SPI_BEGIN() pSPI->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0))

#define CAN_ID_STD  0
#define CAN_ID_EXT  1

#define CAN_IRQ_PIN D1
#define CAN_CS_PIN D2
#define LED         D4
#define LED_ON      (digitalWrite(LED, LOW))
#define LED_OFF     (digitalWrite(LED, HIGH))

char MQTT_Server[20]                  = "192.168.5.1";    // set default value
char MQTT_Port[6]                     = "1883";           // set default value
char MQTT_User[20]                    = "user";           // set default value
char MQTT_Password[20]                = "password";       // set default value

// Desired CAN bus baud rate
#define CAN_BAUDRATE              CAN_125KBPS

// Crystal on the CAN bus shield/module (usually 8 MHz or 16 MHz)
#define CAN_SHIELD_CRYSTAL        MCP_8MHZ

// Topics we subscribe to.
// Messages FROM the MQTT broker to gateway itself
#define TOPIC_MQTT2CAN_CORE          "mqtt2can/core"
// Messages FROM the MQTT broker be forwarded TO the CAN bus network
#define TOPIC_MQTT2CAN_TX          "mqtt2can/canbus/tx/+"

// Topic that we publish our data to.
// This topic will be extended by the CAN bus node ID before publishing,
// so it will become "TOPIC_MQTT2CAN_RX/canbus/0xCANID"
#define TOPIC_MQTT2CAN_RX          "mqtt2can/canbus/rx"

// host name the ESP8266 will show in WLAN
#define HOST_NAME                     "esp8266_mqtt2can"

// a lot of serial output, useful mainly for debugging
//#define DEBUG_OUTPUT

// Byte splitter characters for payloads the we receive via MQTT
#define MQTT_TX_PAYLOAD_SPLITTER " .,;"

// Byte splitter characters for messages we send out via MQTT
#define MQTT_RX_PAYLOAD_SPLITTER ","



void IRAM_ATTR MCP2515_ISR();
void Init_MCP();
void ProcessCANMessage();

WiFiClient espClient;


PubSubClient mqttClient;

MCP_CAN  CAN(CAN_CS_PIN);

//flag for saving data
bool shouldSaveConfig = true;

//callback notifying us of the need to save config
void saveConfigCallback()
{
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

/** will be execute once at startup */
void setup()
{
  pinMode(LED, OUTPUT);
  LED_OFF;

  // Serial Port initialisation
  Serial.begin(115200);
  SPI.setFrequency(4000000);
  ESP.wdtDisable();
  
    // wait for everything to become steady:
  delay(500);
  
  Init_MCP();

 //attachInterrupt(CAN_IRQ_PIN, MCP2515_ISR, FALLING); // start interrupt


  // wait for everything to become steady:
  delay(500);


  ESP.wdtEnable(1000);
  
  Mount_SPIFFS();

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "192.168.5.5", MQTT_Server, sizeof(MQTT_Server));
  WiFiManagerParameter custom_mqtt_port("port", "1883", MQTT_Port, sizeof(MQTT_Port));
  WiFiManagerParameter custom_mqtt_user("user", "mqtt_user", MQTT_User, sizeof(MQTT_User));
  WiFiManagerParameter custom_mqtt_password("password", "mqtt_password", MQTT_Password, sizeof(MQTT_Password));

  WiFiManager wifiManager;

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);

  WiFi.hostname(HOST_NAME);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }
  
  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(MQTT_Server, custom_mqtt_server.getValue());
  strcpy(MQTT_Port, custom_mqtt_port.getValue());
  strcpy(MQTT_User, custom_mqtt_user.getValue());
  strcpy(MQTT_Password, custom_mqtt_password.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig)
  {
    Save_Config();
  }
  
  mqttClient.setClient(espClient);

  mqttClient.setServer(MQTT_Server, String(MQTT_Port).toInt());
  mqttClient.setCallback(MQTT_callback);
}

void Init_MCP()
{
  Serial.println("CAN BUS Shield initialization... ");
  LED_ON;
  while (CAN_OK != CAN.begin(MCP_ANY, CAN_BAUDRATE, CAN_SHIELD_CRYSTAL))
  {
    Serial.println("CAN BUS Shield init failed.");
    Serial.println(" Init CAN BUS Shield again...");
    
    delay(100);
    LED_OFF;
    delay(900);
    LED_ON;
  }
  LED_OFF;  
  Serial.println("CAN BUS Shield init ok!");
  
  /*
     set mask, set filter
  */
  CAN.init_Mask(0, CAN_ID_STD, 0);  // accept ALL messages with standard identifier
  CAN.init_Mask(1, CAN_ID_EXT, 0);  // accept ALL messages with extended identifier 

  CAN.setMode(MCP_NORMAL);                     // Set operation mode to normal so the MCP2515 sends acks to received data.   
}
void Mount_SPIFFS()
{
  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin())
  {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json"))
    {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile)
      {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        //DynamicJsonBuffer jsonBuffer;
        DynamicJsonDocument json(1024);
        DeserializationError error = deserializeJson(json, buf.get());        
        //JsonObject& json = jsonBuffer.parseObject(buf.get());
        serializeJson(json, Serial);
        if (error)
        {
          Serial.println("failed to load json config");  
          return;
        }        
         
        Serial.println("\nparsed json");

        strcpy(MQTT_Server,   json["mqtt_server"]);
        strcpy(MQTT_Port,     json["mqtt_port"]);
        strcpy(MQTT_User,     json["mqtt_user"]);
        strcpy(MQTT_Password, json["mqtt_password"]);
        
        Serial.println(MQTT_Server);
        Serial.println(MQTT_Port);
        Serial.println(MQTT_User);
        Serial.println(MQTT_Password);
      }
    }
  }
  else
  {
    Serial.println("failed to mount FS");
  }
  //end read
}

void Save_Config()
{
  Serial.println("saving config");
  DynamicJsonDocument json(1024);  
  json["mqtt_server"] = MQTT_Server;
  json["mqtt_port"] = MQTT_Port;
  json["mqtt_user"] = MQTT_User;
  json["mqtt_password"] = MQTT_Password;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile)
  {
    Serial.println("failed to open config file for writing");
  }

  serializeJsonPretty(json, Serial);
  serializeJson(json, configFile);
  configFile.close();
  //end save
}

void MQTT_callback(char* topic, byte* payload, unsigned int length)
{
  byte l_TX_Buffer[8] = {0};
  char *l_ptr;
  char *l_ptr2;
  uint_fast8_t l_RTR = 0;
  uint32_t l_CAN_ID = 0;
  uint_fast8_t l_ID_Type = CAN_ID_STD;
  uint_fast8_t l_DLC = 1;

  payload[length] = 0;

  #ifdef DEBUG_OUTPUT
  Serial.print("MQTT Message arrived, Topic [");
  Serial.print(topic);
  Serial.print("], Payload [");
  Serial.print((char*)payload);
  Serial.println("]");
  #endif

  if(strcmp(topic, TOPIC_MQTT2CAN_CORE) == 0)
  {
    if(strcmp((char*)payload, "reboot") == 0)
    {
      Serial.print("Rebooting MQTT-to-CAN gateway..."); 
      ESP.restart();
    }

    return;
  }
  /********* Parsing of the MQTT message **********
  * expects to have the last part of the topic to define the CAN ID and frame type: 
  * mqtt/topic/123
  * mqtt/topic/123r
  * mqtt/topic/123R
  */
  l_ptr = strtok(topic, "/");

  // find last element of topic:
  while (l_ptr != NULL)
  {
    Serial.print(l_ptr);
    Serial.print("/");    
    l_ptr2 = l_ptr; // reminder to the start position of the ID substring
    l_ptr = strtok (NULL, "/");
  }

  
  l_ptr = l_ptr2 + strlen (l_ptr2)-1;

  //Serial.println(*l_ptr);
  if(   (*l_ptr == 'r')
      ||(*l_ptr == 'R'))
  {
    l_RTR = 1;
  }

  l_CAN_ID = strtol(l_ptr2, NULL, 0);
  if(l_CAN_ID > 0x7FF)
  {
    l_ID_Type = CAN_ID_EXT;
  }
  Serial.println("");

  
  /************************************************/

  
  /* Split off payload into up to 8 elements
  *  Each element will represent one byte for the CAN message to be transmitted
  *  Regardless of the format of the specific element, only the lowest 8 bits will 
  *  be taken as content for the CAN bus message byte.
  */
  l_ptr = strtok((char*)payload, MQTT_TX_PAYLOAD_SPLITTER);
  if(l_ptr == NULL)
  {
    l_DLC = 0;
  }
  else
  {
    while (l_ptr != NULL)
    {
      l_TX_Buffer[l_DLC-1] = strtol(l_ptr, NULL, 0) & 0xFF;
  
      Serial.print(l_ptr);
      Serial.print(" ");             
      l_ptr = strtok(NULL, MQTT_RX_PAYLOAD_SPLITTER);
      if (l_ptr != NULL)
      {
        l_DLC++;
  
        if (l_DLC > 8)
        {
          Serial.println("Payload to long for CAN transmission (>8 Bytes). Truncating.");
          l_DLC -= 1;
          break;
        }
      }
    }
    Serial.println("");    
  }
  
  /****************************************************/

  #ifdef DEBUG_OUTPUT
  Serial.print("Transmitting CAN message with ");

  if(l_ID_Type == CAN_ID_EXT)
  {
    Serial.print("extended ID[");
  }
  else
  {
    Serial.print("standard ID[");
  }
  Serial.print("0x");
  Serial.print(l_CAN_ID, HEX);
  Serial.print("], ");
  
  Serial.print("DLC=");
  Serial.print(l_DLC);
  Serial.print(", ");

  Serial.print("Data[ ");
  for (byte i = 0; i < l_DLC; i++)
  {
    Serial.print("0x");
    Serial.print(l_TX_Buffer[i], HEX);
    Serial.print(" ");
  }
  Serial.print("] ");
  
  if (l_RTR)
  {
    Serial.println("as RTR frame.");
  }
  else
  {
    Serial.println("as DATA frame.");
  }
  #endif
  LED_ON;  
  CAN.sendMsgBuf(l_CAN_ID, l_ID_Type, l_DLC, l_TX_Buffer);
  LED_OFF;  
}



void reconnect()
{
  // Loop until we're reconnected
  LED_ON;
  while (!mqttClient.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // If you do not want to use a username and password, change next line to
    if (mqttClient.connect(HOST_NAME, MQTT_User, MQTT_Password))
    {
      Serial.println("connected");

      mqttClient.subscribe(TOPIC_MQTT2CAN_TX);
      mqttClient.subscribe(TOPIC_MQTT2CAN_CORE);
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds...");

      // Wait 4 seconds before retrying
      delay(50);
      LED_OFF;
      delay(950);
      LED_ON;
      delay(50);
      LED_OFF;
      delay(950);
      LED_ON;
      delay(50);
      LED_OFF;
      delay(950);
      LED_ON;
      delay(50);
      LED_OFF;
      delay(950);
      LED_ON;
    }
  }
  LED_OFF;
}

void ProcessCANMessage()
{
  uint8_t l_DLC = 0;
  long unsigned int l_CAN_ID = 0;
  byte l_Ext_ID = 0; 
  byte l_RX_Buffer[8] = {0};
  char l_State_Topic[50];
  char l_Payload[50];
  char l_temp[20];

  if (CAN.checkReceive() == CAN_MSGAVAIL)
  {
    LED_ON;    
    // read data,  len: data length, buf: data buf
    CAN.readMsgBuf(&l_CAN_ID, &l_Ext_ID, &l_DLC, l_RX_Buffer);

    #ifdef DEBUG_EN
    Serial.print("CAN Message arrived ID[");
    Serial.print("0x");
    Serial.print(l_CAN_ID, HEX);
    Serial.print("], DLC=");
    Serial.print(l_DLC);
    Serial.print(", Data[ ");
    for (uint_fast8_t i = 0; i < l_DLC; i++)
    {
      Serial.print(l_RX_Buffer[i]);
      Serial.print(' ');
    }
    Serial.println("]");
    #endif
  LED_OFF;    
  }

  strcpy(l_State_Topic, TOPIC_MQTT2CAN_RX);

  strcat(l_State_Topic, "/0x");
  
  itoa(l_CAN_ID, l_temp, 16);
  strcat(l_State_Topic, l_temp);

  //if(CAN.isRemoteRequest())
  if((l_CAN_ID & 0x40000000) == 0x40000000)   // Determine if message is a remote request frame.
  {
    strcat(l_State_Topic, "R");
  }

  itoa(l_RX_Buffer[0], l_temp, 10);
  strcpy(l_Payload, l_temp);
  for (int i = 1; i < l_DLC; i++)
  {
    strcat(l_Payload, MQTT_RX_PAYLOAD_SPLITTER);
    itoa(l_RX_Buffer[i], l_temp, 10);
    strcat(l_Payload, l_temp);
  }

  #ifdef DEBUG_EN
  Serial.print("Publishing on topic [");
  Serial.print(l_State_Topic);
  Serial.print("] ");
  Serial.println(l_Payload);
  #endif

  mqttClient.publish(l_State_Topic, l_Payload);
}
void MCP2515_ISR()
{
  ProcessCANMessage();
}

void loop()
{
  if (CAN.checkError())
  {
  //  Init_MCP();
  }    

  if(!digitalRead(CAN_IRQ_PIN))
  {
    ProcessCANMessage();
  }

  if (!mqttClient.connected())
  {
    reconnect();
  }

  mqttClient.loop();
}

#include <FS.h> //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h> //https://github.com/bblanchon/ArduinoJson

#include <mcp_can.h>
#include <mcp2515_can.h>
#include <SPI.h>

//#define USE_ETHERNET  // for easy changing to Arduino UNO with ethernet shield - NOT WORKING YET!

#ifdef USE_ETHERNET
#include <Ethernet.h>
#endif

#define SPI_BEGIN() pSPI->beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0))

#define CAN_ID_STD  0
#define CAN_ID_EXT  1

#define CAN_IRQ_PIN 5 // blue WEMOS D1
#define CAN_CS_PIN 2 // blue WEMOS D1
#define LED         2
#define LED_ON      (digitalWrite(LED, HIGH))
#define LED_OFF     (digitalWrite(LED, LOW))

char MQTT_Server[20]                  = "192.168.5.1";    // set default value
char MQTT_Port[6]                     = "1883";           // set default value
char MQTT_User[20]                    = "user";           // set default value
char MQTT_Password[20]                = "password";       // set default value

// topics we subscribe to:
#define TOPIC_MQTT2CAN_TX          "mqtt2can/tx/can_node/+"         // message FROM the MQTT broker to be forwarded to the CAN client as a DATA frame
// topics that we publish our data to:
#define TOPIC_MQTT2CAN_RX          "mqtt2can/rx/can_node" // this part of the topic will be extended by the CAN bus node ID before publishing

#define HOST_NAME                     "esp8266_mqtt2can"

#define DEBUG_OUTPUT


void ICACHE_RAM_ATTR MCP2515_ISR();

byte mac[] = {
  0x00, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5
};


//const int SPI_CS_PIN = 7; // pin 9 on arduino uno

#ifdef USE_ETHERNET
EthernetClient ethClient;
#else
WiFiClient espClient;
#endif
PubSubClient mqttClient;

mcp2515_can CAN(CAN_CS_PIN); // blaues WEMOS D1



//flag for saving data
bool shouldSaveConfig = true;

//callback notifying us of the need to save config
void saveConfigCallback()
{
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

/** Wird beim Start einmal ausgeführt */
void setup()
{
  pinMode(LED, OUTPUT);
  LED_OFF;

  //pinMode(15, OUTPUT);
  //digitalWrite(15, HIGH);
  pinMode(16, INPUT);
  //digitalWrite(15, HIGH);

  // Serial Port initialisation
  Serial.begin(115200);
  SPI.setFrequency(4000000);
  ESP.wdtDisable();
  
  #ifdef DEBUG_OUTPUT
  Serial.print("CAN BUS Shield initialization... ");
  #endif
  LED_ON;
  while (CAN_OK != CAN.begin(CAN_125KBPS, MCP_16MHz))              // init can bus : baudrate = 125k
  {
    #ifdef DEBUG_OUTPUT
    Serial.println("failed :( ...");
    #endif

    delay(50);
    LED_OFF;
    delay(950);
    LED_ON;
  }
  #ifdef DEBUG_OUTPUT
  Serial.println("OK!");
  #endif
  
 attachInterrupt(CAN_IRQ_PIN, MCP2515_ISR, FALLING); // start interrupt

  CAN.init_Mask(0, CAN_ID_STD, 0);  // accept ALL messages with standard identifier
  CAN.init_Mask(1, CAN_ID_EXT, 0);  // accept ALL messages with extended identifier

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

#ifndef USE_ETHERNET
  WiFi.hostname(HOST_NAME);

  //reset settings - for testing
  //wifiManager.resetSettings();

  /*
    if (digitalRead(SWITCH_OPEN_IN))
    {
    if (!wifiManager.startConfigPortal((String("ESP") + String(ESP.getChipId())).c_str()))
    {
          Serial.println("failed to connect and hit timeout");
          delay(3000);
          //reset and try again, or maybe put it to deep sleep
          ESP.reset();
          delay(5000);
    }
    }
  */

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
  #else
  Serial.print("Waiting for an IP address...");
  Ethernet.begin(mac);
  Serial.print("My IP address: ");
  Serial.print(Ethernet.localIP());
  #endif
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

  #ifdef USE_ETHERNET
  mqttClient.setClient(ethClient);
  #else
  mqttClient.setClient(espClient);
  #endif

  mqttClient.setServer(MQTT_Server, String(MQTT_Port).toInt());
  mqttClient.setCallback(MQTT_callback);
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
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success())
        {
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
        else
        {
          Serial.println("failed to load json config");
        }
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
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["mqtt_server"] = MQTT_Server;
  json["mqtt_port"] = MQTT_Port;
  json["mqtt_user"] = MQTT_User;
  json["mqtt_password"] = MQTT_Password;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile)
  {
    Serial.println("failed to open config file for writing");
  }

  json.prettyPrintTo(Serial);
  json.printTo(configFile);
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
    Serial.println(l_ptr);
    l_ptr2 = l_ptr; // reminder to the start position of the ID substring
    l_ptr = strtok (NULL, "/");
  }

  
  l_ptr = l_ptr2 + strlen (l_ptr2)-1;

  Serial.println(*l_ptr);
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

  
  /************************************************/

  
  /* Split off payload into up to 8 elements
  *  Each element will represent one byte for the CAN message to be transmitted
  *  Regardless of the format of the specific element, only the lowest 8 bits will 
  *  be taken as content for the CAN bus message byte.
  */
  l_ptr = strtok((char*)payload, " ,.-");
  while (l_ptr != NULL)
  {
    l_TX_Buffer[l_DLC-1] = (byte)strtol(l_ptr, NULL, 0);

    l_ptr = strtok(NULL, " ,.-");
    if (l_ptr != NULL)
    {
      l_DLC++;

      if (l_DLC > 8)
      {
        Serial.println("Payload to long for CAN transmission (>8 Bytes). Truncating.");
        break;
      }
    }
  }
  l_DLC -= 1;
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

  CAN.sendMsgBuf(l_CAN_ID, l_ID_Type, l_RTR, l_DLC, l_TX_Buffer, 0);
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
      //digitalWrite(LED, LOW);
      //digitalWrite(LED, HIGH);
      mqttClient.subscribe(TOPIC_MQTT2CAN_TX);
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
}

void MCP2515_ISR()
{
  uint8_t l_DLC = 0;
  uint32_t l_CAN_ID = 0;
  byte l_RX_Buffer[8] = {0};
  char l_State_Topic[50];
  char l_Payload[50];
  char l_temp[20];

  while (CAN.checkReceive() == CAN_MSGAVAIL)
  {
    // read data,  len: data length, buf: data buf
    CAN.readMsgBuf(&l_DLC, l_RX_Buffer);

    l_CAN_ID = CAN.getCanId();

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
  }

  strcpy(l_State_Topic, TOPIC_MQTT2CAN_RX);

  strcat(l_State_Topic, "/0x");
  
  itoa(l_CAN_ID, l_temp, 16);
  strcat(l_State_Topic, l_temp);

  if(CAN.isRemoteRequest())
  {
    strcat(l_State_Topic, "R");
  }

  itoa(l_RX_Buffer[0], l_temp, 10);
  strcpy(l_Payload, l_temp);
  for (int i = 1; i < l_DLC; i++)
  {
    strcat(l_Payload, " ");
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

void loop()
{
  if (!mqttClient.connected())
  {
    reconnect();
  }

  mqttClient.loop();

#ifdef USE_ETHERNET
  switch (Ethernet.maintain())
  {
    case 1:
      //renewed fail
      Serial.println("Error: renewed fail");
      break;

    case 2:
      //renewed success
      Serial.println("Renewed success");

      //print your local IP address:
      Serial.print("My IP address: ");
      Serial.print(Ethernet.localIP());
      break;

    case 3:
      //rebind fail
      Serial.println("Error: rebind fail");
      break;

    case 4:
      //rebind success
      Serial.println("Rebind success");

      //print your local IP address:
      Serial.print("My IP address: ");
      Serial.print(Ethernet.localIP());
      break;

    default:
      //nothing happened
      break;
  }
#endif
}

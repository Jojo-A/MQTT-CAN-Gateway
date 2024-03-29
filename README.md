# MQTT-CAN-Gateway
## A fully transparent MQTT-CAN-Bus gateway

This software converts messages from MQTT to CAN-bus and back. Target system is an ESP8266.
It is programmed in Arduino IDE. The board manager needs to be extended to support the ESP boards.
The following additional libraries are needed:
- ArduinoJson https://github.com/bblanchon/ArduinoJson
- PubSubClient https://github.com/knolleary/pubsubclient
- WiFiManager https://github.com/tzapu/WiFiManager
- mcp_can https://github.com/timurrrr/arduino-CAN

We subscribe to the following topic:
```
mqtt2can/canbus/tx/+
```
When receiving an MQTT message on that topic, the last part of the topic (the '+' wildcard) is expected to be the CAN-bus identifier (hex or decimal format), followed by an optional marker if it is an RTR frame (similar to can-utils cansend syntax).
For IDs < 2048 the ID type is treated as "standard" (11-bit). For CAN IDs >= 2048 the ID type is treated as "extended" (29-bit).
The payload is expecte to be in numeric format, separated bytewise by commas ',', dots '.' or spaces ' '. The payload may be in hex or decimal format. The DLC is calculated automatically. Payloads > 8 byte will be truncated.
Example:
```
mqtt2can/canbus/tx/0x601/0x55, 0xAA, 11, 213, 0, 0x01

Identifier: 0x601
ID type: standard
data bytes: 6
Data field: [85 170 11 213 0 1]
```
A CAN message will then be transmitted with the parameters/information gathered above.

When a CAN message is received, it will be split up into its parameters/information as shown above. After that the last part of the MQTT publish-topic will be constructed. The payload will be a string of the received bytes, separated be commas.
Example:
```
received CAN message:

Identifier: 0x51447
ID type: extended
data bytes: 4
Data field: [0xDE 0xAD 0xBE 0xEF]

mqtt2can/canbus/rx/0x51447/8,36,60,125
```


It is planned to work both on an ESP8266 board and alternatively on an Arduino Uno (not yet!!!)

### ESP8266 variant
this variant recommends to use a WeMos D1 R2 ESP8266 board with the form factor and pin header style identical to the Arduino UNO board (see [HERE](https://de.banggood.com/D1-R2-WiFi-ESP8266-Development-Board-Compatible-UNO-Program-By-IDE-p-1011870.html?cur_warehouse=CN)). When using such board it is recommended to also use a [UNO-compatible CAN-Bus shield](https://wiki.seeedstudio.com/CAN-BUS_Shield_V2.0/).
But an ordinary MCP2515-based SPI CAN-bus module (see [HERE](https://de.banggood.com/MCP2515-CAN-Bus-Module-Board-TJA1050-Receiver-SPI-51-MCU-ARM-Controller-5V-DC-p-1481199.html?cur_warehouse=CN&rmmds=search)) will work, too.

*ATTENTION (really):*
Because the CAN Bus shield is supplied with 5V and the ESP8266 is not (officially) 5V-tolerant, one might need a voltage divider at the MISO and the INT pin which go from the CAN Bus shield to the ESP! In fact it also works without any voltage dividers. But don't blame me if you frie your ESP chip.


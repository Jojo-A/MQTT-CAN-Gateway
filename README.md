# MQTT-CAN-Gateway
## A fully trasparent MQTT-CAN-Bus gateway

This software converts messages from MQTT to CAN-bus and back.

When receiving an MQTT message on the topic we've subscribed to, the last part of the topic is expected to be the CAN-bus identifier, followed by an identifier type marker (s/S or e/E) and the data length code (DLC, 0..8).
The payload is expecte to be in numeric format, separated bytewise by commas ','.
Example:
```
this/is/the/subscription/topic/0x514s6/84,36,0,5,8,155

Identifier: 0x514
ID type: standard
data bytes: 6
Data field: [84 36 0 5 8 155]
```
A CAN message will then be transmitted with the parameters/information gathered above.

When a CAN message is received, it will be split up into its parameters/information as shown above. After that the last part of the MQTT publish-topic will be constructed. The payload will be a string of the received bytes, separated be commas.
Example:
```
received CAN message:

Identifier: 0x51447
ID type: extended
data bytes: 4
Data field: [8 36 60 125]

this/is/the/publish/topic/0x51447e4/8,36,60,125
```


It is supposed to work either on an ESP8266 board or alternatively on an Arduino Uno

### ESP8266 variant
this variant recommends to use a WeMos D1 R2 ESP8266 board with the form factor and pin header style identical to the Arduino UNO board (see [HERE](https://de.banggood.com/D1-R2-WiFi-ESP8266-Development-Board-Compatible-UNO-Program-By-IDE-p-1011870.html?cur_warehouse=CN)). When using such board it is recommended to also use a [UNO-compatible CAN-Bus shield](https://wiki.seeedstudio.com/CAN-BUS_Shield_V2.0/).
But an ordinary MCP2515-based SPI CAN-bus module (see [HERE](https://de.banggood.com/MCP2515-CAN-Bus-Module-Board-TJA1050-Receiver-SPI-51-MCU-ARM-Controller-5V-DC-p-1481199.html?cur_warehouse=CN&rmmds=search)) will work, too.

### Arduino UNO variant
this variant uses an Arduino UNO together with an ethernet shield and additionally either an [UNO-compatibel CAN-bus shield](https://wiki.seeedstudio.com/CAN-BUS_Shield_V2.0/) or an ordinary MCP2515-based SPI CAN-bus module (see [HERE](https://de.banggood.com/MCP2515-CAN-Bus-Module-Board-TJA1050-Receiver-SPI-51-MCU-ARM-Controller-5V-DC-p-1481199.html?cur_warehouse=CN&rmmds=search)).

I personally recommend using the ESP8266, because it has a more powerful CPU to handle all the standard C string conversion routines. But as we already know it from most other Arduino projects, noone cares about computation efficiency on the tiny 8-bit cores...

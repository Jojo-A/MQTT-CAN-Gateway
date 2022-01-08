# MQTT-CAN-Gateway
A fully trasparent MQTT-CAN-Bus gateway

It is supposed to work either on an ESP8266 board or alternatively on an Arduino Uno

ESP8266 variant
this variant recommends to use a WeMos D1 R2 ESP8266 board with the form factor and pin header style identical to the Arduino UNO board. When using such board it is recommended to also use a UNO-compatible CAN-Bus shield (like THIS one).
But an ordinary MCP2515-based SPI CAN-bus module like THIS on will work, too.

Arduino UNO variant
this variant uses an Arduino UNO together with an ethernet shield and additionally either an UNO-compatibel CAN-bus shueld or an ordinary MCP2515-based SPI CAN-bus module.

I personally recommend using the ESP8266, because it has a more powerful CPU to handle all the standard C string conversion routines. But as we already know it from most other Arduino projects, noone cares about computation efficiency on the tiny 8-bit cores...

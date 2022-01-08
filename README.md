# MQTT-CAN-Gateway
A fully trasparent MQTT-CAN-Bus gateway

It is supposed to work either on an ESP8266 board or alternatively on an Arduino Uno

ESP8266 variant
this variant recommends to use a WeMos D1 R2 ESP8266 board with the form factor and pin header style identical to the Arduino UNO board. When using such board it is recommended to also use a UNO-compatible CAN-Bus shield (like THIS one).
But a usual SPI CAN-bus module like THIS on will work, too.
Both solutions require a SPI CAN-Bus module, based on a MCP2515. Note: wiring 

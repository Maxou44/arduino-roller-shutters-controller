# Arduino roller shutter controller

Work in progress project to connect roller shutters by simulating a press on the remote button.

An Arduino can support multiple shutters at the same time, we need one pin by button (up and down).

TODO:
- Documentation
- Split and reorganize code
- Avoid to start a new event if the shutter is moving
- Home Assistant configuration

BUGS:
- If MQTT connection is lost, the Arduino will resend a 100/open signal, we have to improve that to report the current status
- If MQTT or wifi connection is lost, the arduino will not be able to stop the shutters (We should stop all the current actions before restarting the connection)

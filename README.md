# Arduino roller shutter controller

Work in progress project to connect roller shutters by simulating a press on the remote button.

An Arduino can support multiple shutters at the same time, we need one pin by button (up and down).

TODO:
- Documentation
- Split and reorganize code
- Avoid to start a new event if the shutter is moving
- Improve loging
- Use MQTT to receive and send events
- Home Assistant configuration
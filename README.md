# Arduino roller shutters controller

Work in progress project to connect roller shutters by simulating a press on the remote button.

- At start up, the arduino will open all the roller shutters to be sure everything has a good state.
- The Arduino can support multiple shutters at the same time, we need one pin by remote button (up and down).
- To stop in progress. action, arduino will retrigger the same signal (up or down).
- The Arduino can control many roller shutters at the same time, in the example, we will control 5 roller shutters.
- The position is calculated using a time calibration.
- For 0 and 100 position the Arduino will not send a STOP signal (To avoid issues with unperfect calibrations).
- A Home Assistant configuration sample is available in ha-configuration.yaml.

Supported features:
- MQTT Action: Open
- MQTT Action: Close
- MQTT Action: Set position
- MQTT Feedback: Position
- MQTT Feedback: State

To do:
- Documentation
- Split and reorganize code
- Avoid to start a new event if the shutter is moving

Bugs:
- If MQTT connection is lost, the Arduino will resend a 100/open signal, we have to improve that to report the current status
- If MQTT or wifi connection is lost, the arduino will not be able to stop the shutters (We should stop all the current actions before restarting the connection)

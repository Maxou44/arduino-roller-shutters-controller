#include <WiFiNINA.h>
#include <ArduinoMqttClient.h>

void (*resetFunc)(void) = 0;

char ssid[] = "";                   // your network SSID (name)
char pass[] = "";  // your network password (use for WPA, or use as key for WEP)

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

const char broker[] = "192.168.1.12";
int port = 1883;


struct RollerShutter {
  // The duration of the shutter to fully open, in ms
  long duration;

  // The opening pin
  int openingPin;

  // The closing pin
  int closingPin;

  // Shutter name
  char name[100];
};

// Roller shutter configuration

#define nbRollerShutters 5

struct RollerShutter rollerShutterList[] = {
  { 17000L, 7, 6, "living-room-big" },    // Living room, big window (07)
  { 17000L, 3, 2, "living-room-small" },  // Living room, small window (16)
  { 17000L, 9, 8, "office" },             // Office (03)
  { 17000L, 5, 4, "bedroom-1" },          // Maxou's bedroom (12)
  { 17000L, 11, 10, "bedroom-2" },        // Guest bedroom (15)
};

extern struct RollerShutter rollerShutterList[];

// For each index of the array, contains 0, 1 or -1 to know the current direction
// If 0, the shutter don't move, if -1 the sutter is opening, if 1 the sutter is closing
int currentMovements[nbRollerShutters];

// For each index of the array, contains the duration in ms before resending the signal
long currentDurations[nbRollerShutters];

// For each index of the array, contains the positions between 0 and 100
long currentPositions[nbRollerShutters];


#define UP 1
#define DOWN -1
#define STOP 0
#define DELAY_SIGNAL 200L
#define DELAY_SIGNAL_PENDING 200L

#define TYPE_STATE 1L
#define TYPE_POSITION 2L

void logMoveTo(const char *name, int shutterIdx, long currentPos, long position, long duration, long percent) {
  Serial.print("[");
  Serial.print(shutterIdx, DEC);
  Serial.print("] ");
  Serial.print(name);
  Serial.print(" (");
  Serial.print(currentPos, DEC);
  Serial.print("% -> ");
  Serial.print(position, DEC);
  Serial.print("%) during ");
  Serial.print(duration);
  Serial.print("ms (Apply ");
  Serial.print(percent, DEC);
  Serial.print("% x ");
  Serial.print(rollerShutterList[shutterIdx].duration, DEC);
  Serial.print(" / 100).\n");
}

void logAutoStop(int shutterIdx, int currentMovement, long position) {
  Serial.print("[");
  Serial.print(shutterIdx, DEC);
  Serial.print("] ");
  if (currentMovement == STOP) {
    Serial.print("STOP");
  }
  if (currentMovement == UP) {
    Serial.print("UP");
  }
  if (currentMovement == DOWN) {
    Serial.print("DOWN");
  }
  Serial.print(" (");
  Serial.print(position, DEC);
  Serial.print("%) ");
  Serial.print(" -> STOP.\n");
}

void logSignal(int shutterIdx, const char *event) {
  Serial.print("[");
  Serial.print(shutterIdx, DEC);
  Serial.print("] Send ");
  Serial.print(event);
  Serial.print(".\n");
}

// It will send a signal to a specific pin and returns the delay
long triggerPin(int pin) {
  digitalWrite(pin, HIGH);
  delay(DELAY_SIGNAL);
  digitalWrite(pin, LOW);
  delay(DELAY_SIGNAL_PENDING);
  return DELAY_SIGNAL + DELAY_SIGNAL_PENDING;
}

// It will send the good signal based on current movement
long sendSignal(int shutterIndex, int movement) {
  if (currentMovements[shutterIndex] == STOP && movement == UP) {
    logSignal(shutterIndex, "UP");
    return triggerPin(rollerShutterList[shutterIndex].openingPin);
  } else if (currentMovements[shutterIndex] == STOP && movement == DOWN) {
    logSignal(shutterIndex, "DOWN");
    return triggerPin(rollerShutterList[shutterIndex].closingPin);
  } else if (currentMovements[shutterIndex] == DOWN && movement == UP) {
    logSignal(shutterIndex, "UP+UP");
    return triggerPin(rollerShutterList[shutterIndex].openingPin) + triggerPin(rollerShutterList[shutterIndex].openingPin);
  } else if (currentMovements[shutterIndex] == UP && movement == DOWN) {
    logSignal(shutterIndex, "DOWN+DOWN");
    return triggerPin(rollerShutterList[shutterIndex].closingPin) + triggerPin(rollerShutterList[shutterIndex].closingPin);
  } else if (currentMovements[shutterIndex] != STOP && movement == STOP) {
    logSignal(shutterIndex, "STOP");
    return triggerPin(currentMovements[shutterIndex] == DOWN ? rollerShutterList[shutterIndex].closingPin : rollerShutterList[shutterIndex].openingPin);
  }
  return 0L;
}

// Reduce a time in shutters current durations
void applyReduceTime(long deltaDuration) {
  for (int i = 0; i < nbRollerShutters; i++) {
    currentDurations[i] = currentDurations[i] - deltaDuration < 0L ? 0L : currentDurations[i] - deltaDuration;
  }
}

void sendStateBasedOnPosition(int shutterIndex, long position, int movement) {
  if (movement == STOP) {
    sendMqttMessage(shutterIndex, TYPE_STATE, position == 0 ? "state_opened" : position == 100L ? "state_closed"
                                                                                                : "state_stopped");
  } else if (movement == UP) {
    sendMqttMessage(shutterIndex, TYPE_STATE, "state_opening");
  } else if (movement == DOWN) {
    sendMqttMessage(shutterIndex, TYPE_STATE, "state_closing");
  }
}

void sendPosition(int shutterIndex, long position) {
  char buffer[sizeof(long) * 10 + 1];
  ltoa(100L - position, buffer, 10);
  sendMqttMessage(shutterIndex, TYPE_POSITION, buffer);
}

// Automatically sent a stop event if the timer is equal to 0 and the shutter is active
void autoStopBasedOnDelay() {
  long d = 0L;

  for (int i = 0; i < nbRollerShutters; i++) {
    if (currentDurations[i] <= 0L && currentMovements[i] != STOP) {
      logAutoStop(i, currentMovements[i], currentPositions[i]);
      if (currentPositions[i] > 0L && currentPositions[i] < 100L) {
        d += sendSignal(i, STOP);
      }

      // Send MQTT statuses
      sendStateBasedOnPosition(i, currentPositions[i], STOP);
      sendPosition(i, currentPositions[i]);

      currentDurations[i] = 0L;
      currentMovements[i] = STOP;
    }
  }
  if (d > 0L) {
    applyReduceTime(d);
  }
}

// Move a shutter to a specific position between [0-100]
long moveTo(int shutterIndex, long position) {
  long d = 0;
  long percent = fabs(position - currentPositions[shutterIndex]);
  long duration = (percent * rollerShutterList[shutterIndex].duration / 100L) + ((position == 0L || position == 100L) ? 3L : 0L);

  logMoveTo(position > currentPositions[shutterIndex] ? "DOWN" : "UP", shutterIndex, currentPositions[shutterIndex], position, duration, percent);

  if (position > currentPositions[shutterIndex] || position < currentPositions[shutterIndex] || position == 0L || position == 100L) {
    sendStateBasedOnPosition(shutterIndex, currentPositions[shutterIndex], position > currentPositions[shutterIndex] ? DOWN : UP);
    d = sendSignal(shutterIndex, position > currentPositions[shutterIndex] ? DOWN : UP);
    currentMovements[shutterIndex] = position > currentPositions[shutterIndex] ? DOWN : UP;
    currentDurations[shutterIndex] = duration;
    currentPositions[shutterIndex] = position;
  }

  return d;
}

void onMqttMessage(int messageSize) {
  char topic[50] = "";
  char content[50] = "";
  long delta = 0L;
  int i = 0;

  mqttClient.messageTopic().toCharArray(topic, 100);

  int shutterIndex = getShutterIndexFromTopic(topic);

  // we received a message, print out the topic and contents
  Serial.print("Received a message from topic \"");
  Serial.print(topic);
  Serial.print("\" (");
  Serial.print(shutterIndex);
  Serial.print(") - \"");

  // use the Stream interface to print the contents
  while (mqttClient.available()) {
    content[i] = (char)mqttClient.read();
    content[i + 1] = 0;
    i++;
  }
  Serial.print(content);

  Serial.print("\" (");
  Serial.print(messageSize);
  Serial.println(")");

  if (strcmp(content, "CLOSE") == 0) {
    applyReduceTime(moveTo(shutterIndex, 100));
  } else if (strcmp(content, "OPEN") == 0) {
    applyReduceTime(moveTo(shutterIndex, 0));
  } else if (strcmp(content, "STOP") == 0) {
    applyReduceTime(sendSignal(shutterIndex, STOP));

    // Calc cancelled percent
    delta = 100L * (currentDurations[shutterIndex] - ((currentPositions[shutterIndex] == 0L || currentPositions[shutterIndex] == 100L) ? 3L : 0L)) / rollerShutterList[shutterIndex].duration;
    if (delta < 0) {
      delta = 0;
    }
    currentPositions[shutterIndex] = currentMovements[shutterIndex] == DOWN ? currentPositions[shutterIndex] - delta : currentPositions[shutterIndex] + delta;
    currentMovements[shutterIndex] = STOP;
    currentDurations[shutterIndex] = 0L;

    // Send MQTT statuses
    sendPosition(shutterIndex, currentPositions[shutterIndex]);
    sendStateBasedOnPosition(shutterIndex, currentPositions[shutterIndex], STOP);

  } else {
    long position = strtol(content, NULL, 10);
    if (position >= 0L && position <= 100L && currentDurations[shutterIndex] <= 0 && currentPositions[shutterIndex] != position) {
      applyReduceTime(moveTo(shutterIndex, 100L - position));
    }
  }
}

void sendMqttMessage(char *topic, const char *value) {
  Serial.print("[MQTT] Send \"");
  Serial.print(value);
  Serial.print("\" to \"");
  Serial.print(topic);
  Serial.println("\"");
  mqttClient.beginMessage(topic);
  mqttClient.print(value);
  mqttClient.endMessage();
}

void sendMqttMessage(int shutterIndex, int type, const char *value) {
  char topic[200] = "shutters/";

  strcat(topic, rollerShutterList[shutterIndex].name);
  if (type == TYPE_POSITION) {
    strcat(topic, "/position");
  }
  if (type == TYPE_STATE) {
    strcat(topic, "/state");
  }
  sendMqttMessage(topic, value);
}

int getShutterIndexFromTopic(const char *cmpTopic) {
  char topic[200] = "";

  for (int i = 0; i < nbRollerShutters; i++) {
    // Set topic
    topic[0] = 0;
    strcat(topic, "shutters/");
    strcat(topic, rollerShutterList[i].name);
    strcat(topic, "/set");
    if (strcmp(topic, cmpTopic) == 0) {
      return i;
    }

    // Set position topic
    topic[0] = 0;
    strcat(topic, "shutters/");
    strcat(topic, rollerShutterList[i].name);
    strcat(topic, "/set_position");
    if (strcmp(topic, cmpTopic) == 0) {
      return i;
    }
  }
  return -1;
}

void subscribeMqtt(int shutterIndex) {
  char topic[200] = "";

  strcpy(topic, "shutters/");
  strcat(topic, rollerShutterList[shutterIndex].name);
  strcat(topic, "/set");
  mqttClient.subscribe(topic);

  strcpy(topic, "shutters/");
  strcat(topic, rollerShutterList[shutterIndex].name);
  strcat(topic, "/set_position");
  mqttClient.subscribe(topic);
}

void wifiLoop() {
  int status = WiFi.status();

  if (status != WL_CONNECTED) {
    Serial.print("Connecting to Wifi: ");
    Serial.println(ssid);
    while (status != WL_CONNECTED) {
      status = WiFi.begin(ssid, pass);
      delay(1000);
    }
    Serial.println("Connected to the network!");
    Serial.println();
  }
}

void mqttLoop() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT: ");
    Serial.println(broker);
    if (!mqttClient.connect(broker, port)) {
      Serial.print("MQTT connection failed! Error code = ");
      Serial.println(mqttClient.connectError());
    } else {
      Serial.println("Connected to MQTT!");
      Serial.println();
      // Subscribe
      mqttClient.onMessage(onMqttMessage);
      for (int i = 0; i < nbRollerShutters; i++) {
        // Subscribe
        subscribeMqtt(i);
        sendStateBasedOnPosition(i, currentPositions[i], STOP);
        sendPosition(i, currentPositions[i]);
      }
    }
  }
  mqttClient.poll();
}

// Run at startup
void setup() {
  // Init serial for debug
  Serial.begin(9600);

  Serial.println("[Setup] Hello world :)");

  // Init output pins
  for (int i = 0; i < nbRollerShutters; i++) {
    Serial.print("[Setup] Configuring \"");
    Serial.print(rollerShutterList[i].name);
    Serial.println("\"");

    // Configure pins
    pinMode(rollerShutterList[i].openingPin, OUTPUT);
    pinMode(rollerShutterList[i].closingPin, OUTPUT);

    // Set pin as LOW by default
    digitalWrite(rollerShutterList[i].openingPin, LOW);
    digitalWrite(rollerShutterList[i].closingPin, LOW);

    delay(1000);

    // Open all the roller shutters (to calibrate)
    triggerPin(rollerShutterList[i].openingPin);
    delay(1000);

    // Reset movements, durations and positions
    currentMovements[i] = STOP;
    currentDurations[i] = 0L;
    currentPositions[i] = 0L;
  }

  Serial.println();
}

// Main loop
void loop() {

  // Wifi
  wifiLoop();

  // MQTT
  mqttLoop();

  // Apply auto reduce time
  delay(50L);
  applyReduceTime(50L);

  // Stop shutter if timer <= 0
  autoStopBasedOnDelay();
}

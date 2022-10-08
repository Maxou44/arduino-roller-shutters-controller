struct RollerShutter {
  // The duration of the shutter to fully open, in ms
  long duration;

  // The opening pin
  int openingPin;

  // The closing pin
  int closingPin;
};

// Roller shutter configuration
int nbRollerShutters = 5;
struct RollerShutter rollerShutterList[] = {
  { 20000L, 7, 6 },    // Living room, big window (07)
  { 15000L, 3, 2 },    // Living room, small window (16) - 21s
  { 20000L, 9, 8 },    // Office (03)
  { 20000L, 5, 4 },    // Maxou's bedroom (12)
  { 20000L, 11, 19 },  // Guest bedroom (15)
};

extern struct RollerShutter rollerShutterList[];

// For each index of the array, contains 0, 1 or -1 to know the current direction
// If 0, the shutter don't move, if -1 the sutter is opening, if 1 the sutter is closing
int *currentMovements = (int *)malloc(nbRollerShutters);

// For each index of the array, contains the duration in ms before resending the signal
long *currentDurations = (long *)malloc(nbRollerShutters);

// For each index of the array, contains the positions between 0 and 100
long *currentPositions = (long *)malloc(nbRollerShutters);


#define UP 1
#define DOWN -1
#define STOP 0
#define DELAY_SIGNAL 200L
#define DELAY_SIGNAL_PENDING 50L

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

// Automatically sent a stop event if the timer is equal to 0 and the shutter is active
void autoStopBasedOnDelay() {
  long d = 0L;

  for (int i = 0; i < nbRollerShutters; i++) {
    if (currentDurations[i] <= 0L && currentMovements[i] != STOP) {
      logAutoStop(i, currentMovements[i], currentPositions[i]);
      if (currentPositions[i] > 1L && currentPositions[i] < 99L) {
        d += sendSignal(i, STOP);
      }        
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
  long percent = abs(position - currentPositions[shutterIndex]);
  long duration = (percent * rollerShutterList[shutterIndex].duration / 100L) + ((position <= 1L || position >= 99L) ? 3L : 0L);

  logMoveTo(position > currentPositions[shutterIndex] ? "DOWN" : "UP", shutterIndex, currentPositions[shutterIndex], position, duration, percent);

  if (position > currentPositions[shutterIndex] || position < currentPositions[shutterIndex] || position <= 1L || position >= 99L) {
    d = sendSignal(shutterIndex, position > currentPositions[shutterIndex] ? DOWN : UP);
    currentMovements[shutterIndex] = position > currentPositions[shutterIndex] ? DOWN : UP;
    currentDurations[shutterIndex] = duration;
    currentPositions[shutterIndex] = position;
  }

  return d;
}

// Run at startup
void setup() {
  // Init serial for debug
  Serial.begin(9600);

  // Init output pins
  for (int i = 0; i < nbRollerShutters; i++) {
    // Configure pins
    pinMode(rollerShutterList[i].openingPin, OUTPUT);
    pinMode(rollerShutterList[i].closingPin, OUTPUT);

    // Set pin as LOW by default
    digitalWrite(rollerShutterList[i].openingPin, LOW);
    digitalWrite(rollerShutterList[i].closingPin, LOW);

    // Reset movements, durations and positions
    currentMovements[i] = STOP;
    currentDurations[i] = 0L;
    currentPositions[i] = 0L;
  }
}

// Main loop
void loop() {
  // put your main code here, to run repeatedly:

  // TODO actions


  if (Serial.available() != 0) {
    long position = Serial.parseInt();
    applyReduceTime(moveTo(1, position));
    //moveTo(1, position);
  }

  delay(50L);
  applyReduceTime(50L);

  // Stop shutter if timer <= 0
  autoStopBasedOnDelay();
}
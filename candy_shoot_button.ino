/*
 * Candy Shoot Button - Arduino Uno R3 + SG90 servo
 * ================================================
 *
 * The giant button's signal idles at about 3.5 V. A press pulls it down, noisily,
 * for about 5 seconds, and the sketch counts it once it has stayed down for 3.
 * Each press swings the servo from REST_ANGLE ("off") to FIRE_ANGLE ("on"),
 * holds it there for FIRE_HOLD_MS, then swings it back. One shot per press,
 * however long the button is held. Set TOGGLE_MODE to make each press flip
 * between the two positions instead.
 * *
 * WIRING (step by step in WIRING.md)
 * ----------------------------------
 *   Button signal            -> A0
 *   Button ground (-)        -> GND
 *   100k resistor            -> between A0 and GND (holds A0 at 0 V if the button is unplugged)
 *   Servo orange (signal)    -> pin 9
 *   Servo red (+)            -> 5V
 *   Servo brown (-)          -> GND
 *   470 uF capacitor         -> between 5V and GND, striped leg (-) to GND
 *
 * BUILD SETTINGS
 * --------------
 *   Board    Arduino Uno                   arduino:avr:uno
 *   Library  Servo (comes with the Arduino IDE)
 *   Serial   115200 baud
 *
 *   arduino-cli compile --fqbn arduino:avr:uno candy_shoot_button
 *
 * TESTING
 * -------
 *   - Type f and press Enter in the Serial Monitor to fire without the button.
 *   - The Uno's "L" LED lights for 1 second each time a press is detected.
 *   - The Serial Monitor shows the average A0 voltage for each second, and
 *     counts the low seconds during a press (PRINT_A0_AVERAGES).
 *   - Set PRINT_SIGNAL_LEVELS to true and open Tools > Serial Plotter to see
 *     what the button signal actually looks like on A0.
 */

#include <Servo.h>

// ---------------------------------------------------------------------------
// SETTINGS
// ---------------------------------------------------------------------------

const uint8_t SIGNAL_PIN = A0;

// A0 reads 1023 at this voltage, which is the Uno's own 5 V supply. On USB it
// can sag to about 4.7 V; that only shifts the readings a few percent.
const float ADC_REFERENCE_VOLTS = 5.0;

// The button's signal idles at about 3.4-3.6 V. A press pulls it down for
// about 5 seconds, but not cleanly: it jumps around between roughly 0.5 V and
// 3 V the whole time. So the sketch doesn't judge single readings. It
// averages all the readings in each AVERAGE_WINDOW_MS, then:
//   - a press counts once PRESS_WINDOWS averages in a row are below
//     PRESS_THRESHOLD_VOLTS (3 one-second averages = low for 3 seconds), and
//   - the press is over once an average is back above
//     RELEASE_THRESHOLD_VOLTS. Only then can the next press count.
// Noise can drag single readings down, but it can't hold a whole second's
// average down for 3 seconds in a row.
// The Serial Monitor prints every average, so you can check these numbers
// against what your button actually does. Keep PRESS_THRESHOLD_VOLTS about
// halfway between the idle and pressed averages, and RELEASE_THRESHOLD_VOLTS
// between PRESS_THRESHOLD_VOLTS and the idle average.
const unsigned long AVERAGE_WINDOW_MS = 1000;
const uint8_t PRESS_WINDOWS = 3;
const float PRESS_THRESHOLD_VOLTS = 2.75;
const float RELEASE_THRESHOLD_VOLTS = 3.0;

const uint8_t SERVO_PIN = 9;

// Pulse widths for 0 and 180 degrees. These are the Servo library's defaults
// and get close to the SG90's full travel. If yours swings short, try 500/2500.
const int SERVO_MIN_US = 544;
const int SERVO_MAX_US = 2400;

// Angles run 0-180, with 90 in the middle of the servo's travel. On this
// servo, higher angles turn the arm clockwise as seen looking down on it.
// A shot swings the arm from REST_ANGLE to FIRE_ANGLE and back, here 45
// degrees clockwise. To swing the other way, use
// FIRE_ANGLE = REST_ANGLE - 45.
const int REST_ANGLE = 75;   // "off" - where the servo sits between shots
const int FIRE_ANGLE = 120;  // "on"

// How long to stay at FIRE_ANGLE before heading back. An SG90 needs about
// 150 ms to swing 90 degrees, so keep this above ~200 ms or it may turn
// around before it gets there.
const unsigned long FIRE_HOLD_MS = 500;

// Time allowed for a move to finish before the servo counts as idle again.
// Presses during a shot (or during this time) are ignored.
const unsigned long MOVE_TIME_MS = 1000;

// false = each press goes to FIRE_ANGLE and back to REST_ANGLE.
// true  = each press flips between REST_ANGLE and FIRE_ANGLE and stays there.
const bool TOGGLE_MODE = false;

// true = stop sending pulses once a move is done. Stops the buzzing some
// SG90s make while holding still, but the arm can then be pushed out of
// position by hand or by a spring.
const bool DETACH_WHEN_IDLE = false;

// true = print the A0 voltage 20 times a second for Tools > Serial Plotter.
const bool PRINT_SIGNAL_LEVELS = false;

// true = print each AVERAGE_WINDOW_MS's average A0 voltage to the Serial
// Monitor, with the lowest and highest single readings in it. Skipped while
// PRINT_SIGNAL_LEVELS is true.
const bool PRINT_A0_AVERAGES = true;

const uint8_t STATUS_LED_PIN = LED_BUILTIN;  // the "L" LED, pin 13

// The "L" LED lights for this long each time a press is detected.
const unsigned long LED_ON_MS = 1000;

// ---------------------------------------------------------------------------

Servo servo;

enum ServoState {
  SERVO_IDLE,      // at rest (or parked, in TOGGLE_MODE), ready for a press
  SERVO_AT_FIRE,   // at FIRE_ANGLE, waiting FIRE_HOLD_MS
  SERVO_MOVING,    // finishing a move, waiting MOVE_TIME_MS
};
ServoState servoState = SERVO_IDLE;
unsigned long servoStateSince = 0;
bool toggledOn = false;  // TOGGLE_MODE only: true while parked at FIRE_ANGLE

bool buttonPressed = false;         // from a detected press until the signal comes back up
uint8_t lowWindowsInARow = 0;       // averages below PRESS_THRESHOLD_VOLTS in a row
unsigned long windowStartedAt = 0;  // start of the current averaging window
float windowSumVolts = 0;
unsigned int windowReadings = 0;
float windowLowestVolts = ADC_REFERENCE_VOLTS;
float windowHighestVolts = 0;
float lastVolts = 0;                // most recent single reading
float lastAverageVolts = 0;         // most recent window's average
bool ledOn = false;
unsigned long ledOnSince = 0;       // when the "L" LED was last turned on
unsigned long lastLevelPrintAt = 0;

float readSignalVolts() {
  return analogRead(SIGNAL_PIN) * ADC_REFERENCE_VOLTS / 1023.0;
}

void startWindow(unsigned long now) {
  windowStartedAt = now;
  windowSumVolts = 0;
  windowReadings = 0;
  windowLowestVolts = ADC_REFERENCE_VOLTS;
  windowHighestVolts = 0;
}

// Prints a line like "A0 average 1.85 V   (readings 0.72 to 2.95 V)   low 2 of 3".
void printWindow(float average) {
  Serial.print(F("A0 average "));
  Serial.print(average, 2);
  Serial.print(F(" V   (readings "));
  Serial.print(windowLowestVolts, 2);
  Serial.print(F(" to "));
  Serial.print(windowHighestVolts, 2);
  Serial.print(F(" V)   "));
  if (buttonPressed) {
    Serial.println(F("pressed"));
  } else if (lowWindowsInARow > 0) {
    Serial.print(F("low "));
    Serial.print(lowWindowsInARow);
    Serial.print(F(" of "));
    Serial.println(PRESS_WINDOWS);
  } else {
    Serial.println(F("idle"));
  }
}

// Takes one reading and adds it to the current averaging window. At the end
// of each window, checks its average. Returns true once per press.
bool buttonWasPressed(unsigned long now) {
  lastVolts = readSignalVolts();
  windowSumVolts += lastVolts;
  windowReadings++;
  if (lastVolts < windowLowestVolts) {
    windowLowestVolts = lastVolts;
  }
  if (lastVolts > windowHighestVolts) {
    windowHighestVolts = lastVolts;
  }
  if (now - windowStartedAt < AVERAGE_WINDOW_MS) {
    return false;
  }

  float average = windowSumVolts / windowReadings;
  lastAverageVolts = average;
  if (!buttonPressed) {
    if (average < PRESS_THRESHOLD_VOLTS) {
      lowWindowsInARow++;
    } else {
      lowWindowsInARow = 0;
    }
  }
  if (PRINT_A0_AVERAGES && !PRINT_SIGNAL_LEVELS) {
    printWindow(average);
  }
  startWindow(now);

  if (buttonPressed) {
    if (average > RELEASE_THRESHOLD_VOLTS) {
      buttonPressed = false;
      Serial.println(F("Button released"));
    }
    return false;
  }
  if (lowWindowsInARow >= PRESS_WINDOWS) {
    buttonPressed = true;
    lowWindowsInARow = 0;
    Serial.println(F("Button pressed"));
    return true;
  }
  return false;
}

void turnOnStatusLed(unsigned long now) {
  digitalWrite(STATUS_LED_PIN, HIGH);
  ledOn = true;
  ledOnSince = now;
}

// Turns the "L" LED back off LED_ON_MS after it was turned on.
void updateStatusLed(unsigned long now) {
  if (ledOn && now - ledOnSince >= LED_ON_MS) {
    digitalWrite(STATUS_LED_PIN, LOW);
    ledOn = false;
  }
}

void moveServo(int angle) {
  if (!servo.attached()) {
    // attach() alone would start at the library's default of 90 degrees.
    // Writing straight away replaces that before the first pulse goes out,
    // so the servo doesn't twitch to the middle at power-up.
    servo.attach(SERVO_PIN, SERVO_MIN_US, SERVO_MAX_US);
  }
  servo.write(angle);
}

void setServoState(ServoState state, unsigned long now) {
  servoState = state;
  servoStateSince = now;
}

void fire(const __FlashStringHelper *source, unsigned long now) {
  if (servoState != SERVO_IDLE) {
    Serial.print(F("Ignored "));
    Serial.print(source);
    Serial.println(F(": servo still busy with the last shot"));
    return;
  }

  int target = FIRE_ANGLE;
  if (TOGGLE_MODE) {
    toggledOn = !toggledOn;
    target = toggledOn ? FIRE_ANGLE : REST_ANGLE;
  }
  moveServo(target);
  setServoState(TOGGLE_MODE ? SERVO_MOVING : SERVO_AT_FIRE, now);

  Serial.print(F("FIRE from "));
  Serial.print(source);
  Serial.print(F(" -> servo to "));
  Serial.println(target);
}

void updateServo(unsigned long now) {
  unsigned long elapsed = now - servoStateSince;
  if (servoState == SERVO_AT_FIRE && elapsed >= FIRE_HOLD_MS) {
    moveServo(REST_ANGLE);
    setServoState(SERVO_MOVING, now);
  } else if (servoState == SERVO_MOVING && elapsed >= MOVE_TIME_MS) {
    if (DETACH_WHEN_IDLE) {
      servo.detach();
      // detach() can land in the middle of a pulse and leave the pin stuck
      // HIGH, since the library only ever sets LOW on attached pins.
      digitalWrite(SERVO_PIN, LOW);
    }
    setServoState(SERVO_IDLE, now);
  }
}

void handleSerialCommands(unsigned long now) {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'f' || c == 'F') {
      fire(F("Serial Monitor"), now);
    }
  }
}

void printSignalLevel(unsigned long now) {
  if (now - lastLevelPrintAt < 50) {
    return;
  }
  lastLevelPrintAt = now;
  Serial.print(F("volts:"));
  Serial.print(lastVolts, 2);
  Serial.print(F(" average:"));
  Serial.print(lastAverageVolts, 2);
  Serial.print(F(" press:"));
  Serial.print(PRESS_THRESHOLD_VOLTS, 2);
  Serial.print(F(" release:"));
  Serial.println(RELEASE_THRESHOLD_VOLTS, 2);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("Candy Shoot Button"));

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);  // off

  unsigned long now = millis();
  moveServo(REST_ANGLE);
  setServoState(SERVO_MOVING, now);  // let it reach REST_ANGLE before accepting presses

  // Start from whatever the signal is doing right now, so a button held at
  // power-up (or an input stuck low) can't fire until it comes back up.
  float volts = readSignalVolts();
  buttonPressed = volts < PRESS_THRESHOLD_VOLTS;
  startWindow(now);

  Serial.print(F("A0 is reading "));
  Serial.print(volts, 2);
  Serial.println(F(" V (should be about 3.5 with the button idle)"));
  if (buttonPressed) {
    Serial.println(F("A0 is already low (button held or unplugged) - waiting for it to come back up first"));
  }
  Serial.println(F("Ready. Press the button, or type f + Enter here to fire."));
}

void loop() {
  unsigned long now = millis();

  if (buttonWasPressed(now)) {
    turnOnStatusLed(now);
    fire(F("button"), now);
  }
  handleSerialCommands(now);
  updateServo(now);
  updateStatusLed(now);
  if (PRINT_SIGNAL_LEVELS) {
    printSignalLevel(now);
  }

  delay(5);  // ~200 readings a second, so each 1 second average uses ~200
}

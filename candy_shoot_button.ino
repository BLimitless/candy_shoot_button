/*
 * Candy Shoot Button - Arduino Uno R3 + SG90 servo
 * ================================================
 *
 * Press the giant button -> it puts out about 1.5 V for about 1 second ->
 * the servo swings from REST_ANGLE ("off") to FIRE_ANGLE ("on"), holds there
 * for FIRE_HOLD_MS, then swings back. One shot per press, however long the
 * signal lasts. Set TOGGLE_MODE to make each press flip between the two
 * positions instead.
 * *
 * WIRING (step by step in WIRING.md)
 * ----------------------------------
 *   Button signal (+1.5 V)   -> A0
 *   Button ground (-)        -> GND
 *   100k resistor            -> between A0 and GND (holds A0 at 0 V between presses)
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
 *   - The Uno's "L" LED lights while the button signal is detected.
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

// The signal counts as present once it rises above PRESS_THRESHOLD_VOLTS and
// as gone once it falls below RELEASE_THRESHOLD_VOLTS. The gap between them
// stops a noisy signal hovering near one threshold from flickering. 1.5 V,
// or ~1.2 V once the button's battery is tired, clears 0.9 V comfortably.
const float PRESS_THRESHOLD_VOLTS = 1.0;
const float RELEASE_THRESHOLD_VOLTS = 0.5;

// The signal must stay present this long to count as a press (ignores spikes
// and noise), and stay gone this long before the next press can count (so
// the ~1 second signal fires once, not repeatedly).
const unsigned long PRESS_CONFIRM_MS = 250;
const unsigned long RELEASE_CONFIRM_MS = 250;

const uint8_t SERVO_PIN = 9;

// Pulse widths for 0 and 180 degrees. These are the Servo library's defaults
// and get close to the SG90's full travel. If yours swings short, try 500/2500.
const int SERVO_MIN_US = 544;
const int SERVO_MAX_US = 2400;

const int REST_ANGLE = 0;   // "off" - where the servo sits between shots
const int FIRE_ANGLE = 90;  // "on"

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

const uint8_t STATUS_LED_PIN = LED_BUILTIN;  // the "L" LED, pin 13

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

bool signalPresent = false;             // debounced button signal
unsigned long signalLastMatchedAt = 0;  // last time a raw reading agreed with signalPresent
unsigned long lastLevelPrintAt = 0;

float readSignalVolts() {
  return analogRead(SIGNAL_PIN) * ADC_REFERENCE_VOLTS / 1023.0;
}

// One undebounced reading. The threshold depends on the current debounced
// state, which is what gives the hysteresis.
bool readRawSignal() {
  float threshold = signalPresent ? RELEASE_THRESHOLD_VOLTS : PRESS_THRESHOLD_VOLTS;
  return readSignalVolts() > threshold;
}

// Debounces the button signal. Returns true once per press, PRESS_CONFIRM_MS
// after the signal appears.
bool buttonWasPressed(unsigned long now) {
  bool raw = readRawSignal();
  if (raw == signalPresent) {
    signalLastMatchedAt = now;
    return false;
  }
  unsigned long confirmMs = raw ? PRESS_CONFIRM_MS : RELEASE_CONFIRM_MS;
  if (now - signalLastMatchedAt < confirmMs) {
    return false;
  }

  signalPresent = raw;
  signalLastMatchedAt = now;
  digitalWrite(STATUS_LED_PIN, signalPresent ? HIGH : LOW);
  Serial.println(signalPresent ? F("Button signal on") : F("Button signal off"));
  return signalPresent;
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
  Serial.print(readSignalVolts(), 2);
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
  // power-up (or an input stuck on) can't fire until it turns off and on again.
  signalPresent = readRawSignal();
  signalLastMatchedAt = now;
  digitalWrite(STATUS_LED_PIN, signalPresent ? HIGH : LOW);

  Serial.print(F("A0 is reading "));
  Serial.print(readSignalVolts(), 2);
  Serial.println(F(" V (should be near 0 with the button idle)"));
  if (signalPresent) {
    Serial.println(F("Button signal is already on - waiting for it to turn off first"));
  }
  Serial.println(F("Ready. Press the button, or type f + Enter here to fire."));
}

void loop() {
  unsigned long now = millis();

  if (buttonWasPressed(now)) {
    fire(F("button"), now);
  }
  handleSerialCommands(now);
  updateServo(now);
  if (PRINT_SIGNAL_LEVELS) {
    printSignalLevel(now);
  }

  delay(5);  // ~200 readings a second is plenty for a 1 second signal
}

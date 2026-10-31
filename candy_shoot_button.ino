/*
 * Candy Shoot Button - Arduino Uno R3 + SG90 servo
 * ================================================
 *
 * The giant button's signal sits at 0 V while idle. A press makes it jump
 * around between 0 V and peaks of about 1.5-1.75 V for about 5 seconds, and
 * the sketch counts it once the peaks have kept coming for 3.
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
 *     With no Serial Monitor (on a wall adapter) it's the only sign that the
 *     press was seen.
 *   - The Serial Monitor shows the highest A0 reading (the peak) for each
 *     second, and counts the seconds with signal during a press
 *     (PRINT_A0_PEAKS).
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

// The button's signal sits at 0 V while idle. A press makes it jump around
// for about 5 seconds: single readings land anywhere from 0 V up to peaks of
// about 1.5-1.75 V, so the average swings about, but the peaks keep coming.
// So the sketch:
//   - finds the highest reading (the peak) in each WINDOW_MS,
//   - counts a second as "signal" when its peak reaches SIGNAL_PEAK_VOLTS, and
//   - counts a press once PRESS_WINDOWS seconds in a row have signal.
// A single stray spike can't fire it, because it takes a peak in every one
// of those seconds.
// After the shot, the status goes back to idle once the servo is back at
// rest. The next press only starts counting after a whole second with no
// signal (the button's signal has ended); otherwise the rest of the same
// signal would count as a second press.
// The Serial Monitor prints every second's peak, so you can check these
// numbers against what your button actually does.
const unsigned long WINDOW_MS = 1000;
const uint8_t PRESS_WINDOWS = 3;

// Idle reads 0.00 V and a press peaks at 1.5 V or more, so this sits about
// halfway between. That leaves room for the peaks getting lower, e.g. as the
// button's batteries run down.
const float SIGNAL_PEAK_VOLTS = 0.75;

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

// true = print each WINDOW_MS's peak A0 voltage to the Serial Monitor, with
// the average and lowest single reading in it. Skipped while
// PRINT_SIGNAL_LEVELS is true.
const bool PRINT_A0_PEAKS = true;

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

bool buttonPressed = false;         // from a detected press until the servo is back at rest
bool waitingForSignalToEnd = false; // from a detected press until a second with no signal
uint8_t signalWindowsInARow = 0;    // seconds with signal in a row
unsigned long windowStartedAt = 0;  // start of the current window
float windowSumVolts = 0;
unsigned int windowReadings = 0;
float windowLowestVolts = ADC_REFERENCE_VOLTS;
float windowPeakVolts = 0;          // highest reading in the current window
float lastVolts = 0;                // most recent single reading
float lastPeakVolts = 0;            // most recent window's peak
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
  windowPeakVolts = 0;
}

// Prints a line like
// "A0 peak 1.74 V   (average 0.90 V, lowest 0.01 V)   signal 2 of 3".
void printWindow() {
  Serial.print(F("A0 peak "));
  Serial.print(windowPeakVolts, 2);
  Serial.print(F(" V   (average "));
  Serial.print(windowSumVolts / windowReadings, 2);
  Serial.print(F(" V, lowest "));
  Serial.print(windowLowestVolts, 2);
  Serial.print(F(" V)   "));
  if (buttonPressed) {
    Serial.println(F("pressed"));
  } else if (waitingForSignalToEnd) {
    Serial.println(F("idle (waiting for the last press's signal to end)"));
  } else if (signalWindowsInARow > 0) {
    Serial.print(F("signal "));
    Serial.print(signalWindowsInARow);
    Serial.print(F(" of "));
    Serial.println(PRESS_WINDOWS);
  } else {
    Serial.println(F("idle"));
  }
}

// Takes one reading and adds it to the current window. At the end of each
// window, checks its peak. Returns true once per press.
bool buttonWasPressed(unsigned long now) {
  lastVolts = readSignalVolts();
  windowSumVolts += lastVolts;
  windowReadings++;
  if (lastVolts < windowLowestVolts) {
    windowLowestVolts = lastVolts;
  }
  if (lastVolts > windowPeakVolts) {
    windowPeakVolts = lastVolts;
  }
  if (now - windowStartedAt < WINDOW_MS) {
    return false;
  }

  lastPeakVolts = windowPeakVolts;
  bool hasSignal = windowPeakVolts >= SIGNAL_PEAK_VOLTS;
  if (!hasSignal) {
    signalWindowsInARow = 0;
  } else if (!waitingForSignalToEnd) {
    signalWindowsInARow++;
  }
  if (PRINT_A0_PEAKS && !PRINT_SIGNAL_LEVELS) {
    printWindow();
  }
  startWindow(now);

  if (waitingForSignalToEnd) {
    if (!hasSignal) {
      waitingForSignalToEnd = false;
      Serial.println(F("Button signal ended - ready for the next press"));
    }
    return false;
  }
  if (signalWindowsInARow >= PRESS_WINDOWS) {
    signalWindowsInARow = 0;
    buttonPressed = true;
    waitingForSignalToEnd = true;
    Serial.println(F("Button pressed"));
    return true;
  }
  return false;
}

// The button counts as pressed until its shot is over and the servo is back
// at rest.
void updateButtonStatus() {
  if (buttonPressed && servoState == SERVO_IDLE) {
    buttonPressed = false;
    Serial.println(F("Servo back at rest - button idle"));
  }
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
  Serial.print(F(" peak:"));
  Serial.print(lastPeakVolts, 2);
  Serial.print(F(" threshold:"));
  Serial.println(SIGNAL_PEAK_VOLTS, 2);
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

  startWindow(now);

  Serial.print(F("A0 is reading "));
  Serial.print(readSignalVolts(), 2);
  Serial.println(F(" V (should be about 0 with the button idle)"));
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
  updateButtonStatus();
  updateStatusLed(now);
  if (PRINT_SIGNAL_LEVELS) {
    printSignalLevel(now);
  }

  // ~900 readings a second. The more readings, the surer each second is to
  // catch the signal's peaks.
  delay(1);
}

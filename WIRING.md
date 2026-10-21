# Wiring the Candy Shoot Button (Arduino Uno R3)

## Parts

| Part | Notes |
|---|---|
| Arduino Uno R3 | |
| SG90 micro servo | |
| 100 kΩ resistor | Holds A0 at 0 V between presses. Anything from 10 kΩ to 100 kΩ works. |
| 470 µF electrolytic capacitor, 10 V or higher | Smooths the servo's current spikes so the Uno doesn't reset. 100–1000 µF is fine. |
| Breadboard and jumper wires | Makes the resistor and capacitor easy to hook up |
| Multimeter | For Step 1 |

## Step 1: Find the button's signal and ground wires

Do this before connecting anything to the Uno.

1. Set the multimeter to DC volts.
2. Put the **red** probe on the wire you think is the signal and the **black**
   probe on the wire you think is ground.
3. Press the giant button.

| Reading | Meaning |
|---|---|
| About **+1.5 V** for ~1 s, then back to ~0 V | Correct. Red-probe wire = **SIGNAL**, black-probe wire = **GROUND**. |
| About **−1.5 V** | The wires are swapped. Swap them and measure again. |
| 1.5 V all the time, dropping to 0 V when pressed | The signal is inverted. The sketch expects 0 V idle, 1.5 V pressed. |
| Anything above **5 V** | **Stop.** That would damage A0. |

If the button runs on its own batteries, connecting its ground wire to the
Uno's GND is safe: it just gives the two circuits a shared reference so the
voltage means something. If the button is powered from USB or a wall adapter
instead, its GROUND wire must be the negative (0 V) side of that supply.
Check that the wire reads 0 V against that supply's negative whether the
button is pressed or not.

## Step 2: Connect the button

| From | To |
|---|---|
| Button SIGNAL (+1.5 V) | **A0** |
| Button GROUND | **GND** (any GND pin) |
| 100 kΩ resistor | Between **A0** and **GND** |

Why A0 and not a digital pin: a 5 V Uno only reads a digital pin as HIGH
reliably above 3.0 V, and 1.5 V is right at the edge of what counts as LOW.
A0 measures the actual voltage, so 1.5 V is easy to spot.

Why the resistor: when the button is idle its signal wire may not be
connected to anything inside, which leaves A0 "floating". A floating input
picks up electrical noise and can look like a press. The resistor gently
holds it at 0 V. It's weak enough that the button's 1.5 V easily overrides it.

## Step 3: Connect the servo

SG90 wire colors: **brown = ground (−)**, **red = power (+5 V)**,
**orange = signal**.

| From | To |
|---|---|
| Servo orange (signal) | Pin **9** (labeled ~9) |
| Servo red (+) | **5V** |
| Servo brown (−) | **GND** |
| 470 µF capacitor | Between **5V** and **GND**. The leg by the stripe (−, usually the shorter one) goes to GND. |

Electrolytic capacitors only work one way round. Backwards, they can pop.
The easiest way to wire this is to run 5V and GND from the Uno to a
breadboard's + and − rails, then plug the servo, the capacitor and the button
ground into those rails.

```
                              Arduino Uno R3
                           +----------------+
  Button SIGNAL ----+------| A0          ~9 |----------------- Servo ORANGE
                    |      |                |
                 [100k]    |             5V |----+------------ Servo RED
                    |      |                |    |
  Button GROUND ----+------| GND        GND |----|----+------- Servo BROWN
                           +----------------+    |    |
                                               (+)  (-)
                                              [ 470 uF ]
```

(The two GND connections can share a single GND pin through the breadboard
rail. The Uno has three GND pins and they're all connected.)

### Power

One SG90 runs fine off the Uno's 5V pin when it's powered by USB. Add the
capacitor either way.

If the Uno restarts when the servo moves (the Serial Monitor prints
"Candy Shoot Button" again), the USB port can't keep up. Either:

- Plug a **9 V, 1 A** wall adapter into the Uno's barrel jack. The Uno
  switches over to it automatically, even with USB also plugged in, and the
  5V pin gets more current.
- Or power the servo from its own **5 V** supply (USB charger with a
  breakout, or a 4×AA NiMH pack). Supply + goes to servo red, and supply −
  goes to servo brown **and** to a GND pin on the Uno. Without that shared
  ground the servo can't read the signal. In this setup, don't connect servo
  red to the Uno's 5V pin.

## Step 4: Upload the sketch

**Arduino IDE:** open `candy_shoot_button.ino`, choose **Tools → Board →
Arduino AVR Boards → Arduino Uno**, pick the port (`/dev/cu.usbmodem…` on a
genuine Uno, `/dev/cu.usbserial…` on most clones), and click Upload. The Servo library comes with the IDE.

**arduino-cli**, from the folder that contains `candy_shoot_button`:

```sh
arduino-cli lib install Servo     # one time only
arduino-cli compile --fqbn arduino:avr:uno candy_shoot_button
arduino-cli upload  --fqbn arduino:avr:uno -p /dev/cu.usbmodemXXXX candy_shoot_button
arduino-cli monitor -p /dev/cu.usbmodemXXXX -c baudrate=115200
```

(`arduino-cli board list` shows the port.)

## Step 5: Test it

1. Open the Serial Monitor at **115200** baud. Opening it restarts the Uno,
   which prints `A0 is reading 0.00 V`. With the button idle that number
   should be close to 0.
2. Type **f** and press Enter. The servo should swing to 90° and back.
3. Press the giant button. The Uno's **L** LED lights while the signal is
   detected, and the Serial Monitor prints `Button signal on` then
   `FIRE from button`.
4. Fit the servo arm last. At power-up the servo moves to its rest
   position (0°), so mount the arm in its "off" position after that.
5. Adjust `REST_ANGLE`, `FIRE_ANGLE` and `FIRE_HOLD_MS` at the top of the
   sketch to suit your mechanism.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Uno restarts when the servo moves | Power problem. Check the capacitor, then see [Power](#power). |
| `FIRE` prints but the servo doesn't move | Check servo red goes to 5V, brown to GND and orange to pin 9. With a separate supply, check its − is wired to GND on the Uno. |
| Fires on its own with nothing pressed | The 100 kΩ resistor from A0 to GND is missing, or the button's GROUND wire isn't connected. |
| Button press isn't detected | Set `PRINT_SIGNAL_LEVELS = true`, upload, open **Tools → Serial Plotter** and press the button. You should see a jump to ~1.5 V. Nothing at all: check both button wires. A smaller jump: lower `PRESS_THRESHOLD_VOLTS` to about half of it and `RELEASE_THRESHOLD_VOLTS` to about a quarter. |
| Fires twice per press | The signal dips partway through. Raise `RELEASE_CONFIRM_MS` (e.g. to 500). |
| Servo buzzes or strains at one end | It's pushing against its end stop. Move that angle a few degrees in (e.g. `REST_ANGLE = 5`). |
| Servo jitters while idle | Set `DETACH_WHEN_IDLE = true`. |
| Swings less than expected | Widen the pulse range: `SERVO_MIN_US = 500`, `SERVO_MAX_US = 2500`. |

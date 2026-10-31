# Wiring the Candy Shoot Button (Arduino Uno R3)

## Parts

| Part | Notes |
|---|---|
| Arduino Uno R3 | |
| SG90 micro servo | |
| 100 kΩ resistor | Holds A0 at 0 V if the button is unplugged. 100 kΩ to 1 MΩ works. |
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
| About **0 V** while idle, jumping up for a few seconds when pressed (the meter shows somewhere around 0.5–1 V because it averages; the actual peaks reach about 1.5–1.75 V) | Correct. Red-probe wire = **SIGNAL**, black-probe wire = **GROUND**. |
| Goes **negative** when pressed | The wires are swapped. Swap them and measure again. |
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
| Button SIGNAL | **A0** |
| Button GROUND | **GND** (any GND pin) |
| 100 kΩ resistor | Between **A0** and **GND** |

Why A0 and not a digital pin: a 5 V Uno only reads a digital pin as HIGH
reliably above 3.0 V, and the button's signal only peaks at about
1.5–1.75 V. A0 measures the actual voltage, so the sketch can catch those
peaks even though the signal jumps around.

Why the resistor: if the button's signal wire is ever disconnected, the
resistor holds A0 at a definite 0 V instead of leaving it "floating" and
picking up random voltages. It's weak enough that the button's signal easily
overrides it.

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

A **5 V** wall adapter has to go into the Uno's USB socket (a USB phone
charger and cable). The barrel jack needs **7–12 V**. With 5 V on the barrel
jack, the Uno's 5V pin only gets about 3.5 V.

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
   which prints `A0 is reading` and a voltage. With the button idle that
   number should be about 0. After that it prints the peak (highest) A0
   voltage for each second.
2. Type **f** and press Enter. The servo should swing 45° (to 120°) and back,
   then give one quick extra open-and-close (the wiggle).
3. Press the giant button. The per-second lines count `signal 1 of 3`,
   `signal 2 of 3`, `signal 3 of 3`. Then, about 3 seconds after the press, the
   Uno's **L** LED (next to TX and RX, not the green **ON** power light)
   lights for 1 second and the Serial Monitor prints `Button pressed` then
   `FIRE from button`. Once the servo is back at rest the status goes back
   to idle, and when the button's signal ends it prints
   `Button signal ended - ready for the next press`.
4. Fit the servo arm last. At power-up the servo moves to its rest
   position (75°), so mount the arm in its "off"
   position after that.
5. Adjust `REST_ANGLE`, `FIRE_ANGLE` and `FIRE_HOLD_MS` at the top of the
   sketch to suit your mechanism.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Uno restarts when the servo moves | Power problem. Check the capacitor, then see [Power](#power). |
| `FIRE` prints but the servo doesn't move | Check servo red goes to 5V, brown to GND and orange to pin 9. With a separate supply, check its − is wired to GND on the Uno. |
| Fires on its own with nothing pressed | The `A0 peak` reached `SIGNAL_PEAK_VOLTS` for `PRESS_WINDOWS` lines in a row. Idle peaks should be 0.00 V; if they aren't, check that the button's GROUND wire and the 100 kΩ resistor are connected. Otherwise raise `SIGNAL_PEAK_VOLTS` (keeping it below the peaks a real press gives) or raise `PRESS_WINDOWS`. |
| The per-second lines keep saying `waiting for the last press's signal to end` | Every line's `A0 peak` is still at or above `SIGNAL_PEAK_VOLTS`, so the sketch thinks the button's signal is still going and won't count a new press. If the idle peak never gets back near 0 V, raise `SIGNAL_PEAK_VOLTS` above it (keeping it below a real press's peaks). |
| **L** LED stays on all the time | The current sketch isn't running. Upload it again. |
| Button press isn't detected | Watch the `A0 peak` on each line while pressing. It has to reach `SIGNAL_PEAK_VOLTS` for 3 lines in a row. If the peaks are lower, lower `SIGNAL_PEAK_VOLTS` to about half of them. If the button's signal doesn't last 3 seconds, lower `PRESS_WINDOWS`. For a closer look, set `PRINT_SIGNAL_LEVELS = true` and open **Tools → Serial Plotter**. |
| Works on the laptop but not on a wall adapter | There's no Serial Monitor on a wall adapter, so watch the **L** LED. If it lights for 1 second after a press but the servo doesn't move, the press was detected and it's a power problem (see [Power](#power), including where a 5 V adapter has to plug in). If it doesn't light, the press isn't reaching A0. Check the button's GROUND wire goes to the Uno's GND. |
| Fires twice per press | Partway through the press, a whole second went by with no reading reaching `SIGNAL_PEAK_VOLTS`, so the sketch thought the signal had ended. Lower `SIGNAL_PEAK_VOLTS`. |
| Servo buzzes or strains at one end | It's pushing against its end stop. Move that angle a few degrees in (e.g. 180 → 175, or 0 → 5). |
| Servo jitters while idle | Set `DETACH_WHEN_IDLE = true`. |
| Swings less than expected | Widen the pulse range: `SERVO_MIN_US = 500`, `SERVO_MAX_US = 2500`. |

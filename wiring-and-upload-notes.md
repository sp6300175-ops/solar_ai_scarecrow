# Wiring and upload notes

Practical notes from building this on a breadboard.

## Setup used

```
LAPTOP --USB--> [ESP32-CAM-MB USB base] --jumper wires--> [ESP32-CAM on breadboard]
```

The camera sits on the breadboard, so all the parts can be wired next to it without soldering.
The USB base stays connected to the laptop and supplies power, upload and Serial Monitor.

Jumper wires between the USB base sockets and the camera pins:

| USB base socket | ESP32-CAM pin | Purpose |
|---|---|---|
| 5V | 5V | power |
| GND | GND | ground |
| U0R | U0R | data to the camera |
| U0T | U0T | data from the camera |

The base sockets are unlabeled on some boards. Count by position, using the pin labels printed on the camera:

| Position | Row A | Row B |
|---|---|---|
| 1 | 5V | 3V3 |
| 2 | GND | IO16 |
| 3 | IO12 | IO0 |
| 4 | IO13 | GND |
| 5 | IO15 | VCC |
| 6 | IO14 | U0R |
| 7 | IO2 | U0T |
| 8 | IO4 | GND |

Use the pin named **5V** in Row A for power, not the pin named VCC in Row B.

If upload fails with "No serial data received", swap the U0R and U0T wires once.

## Breadboard power rails

Each 5-hole column on the breadboard is connected, and neighbouring columns are not.
This build uses separate columns for 5V, 3V3 and two GND lines.

- Never put a 5V wire and a GND wire in the same column. That is a short circuit
  (the USB base light turns off when it happens).
- Join the two GND columns with a wire, and connect at least one to the camera GND pin.
- Label each column with tape.

## Boot pins

IO0, IO2, IO12 and IO15 are boot-related pins on the ESP32.

- **IO0** must be LOW only while uploading. If it stays connected to GND while running,
  the camera clock fails and image capture returns errors. Remove the IO0 jumper after upload.
- **IO2 and IO12** (DHT data and LED here): pull these wires out before uploading, then push them back in.
- IO12 must be LOW at boot. An LED with a resistor to GND is fine.

## Upload procedure (breadboard)

1. Unplug the IO2 and IO12 wires.
2. Push a jumper from IO0 to GND.
3. Press RST on the camera.
4. Click Upload in the Arduino IDE.
5. After "Done uploading", remove the IO0 jumper, restore IO2 and IO12, press RST.

## Checking the LED and buzzer

Upload `led_buzzer_test/led_buzzer_test.ino` and watch the four steps (A: LED, B/C/D: buzzer).
The step that makes the buzzer sound sets the buzzer settings in `scarecrow.ino`:

- B sounds: keep `BUZZER_ACTIVE_LOW 1`, `BUZZER_PASSIVE 0` (default)
- C sounds: set `BUZZER_ACTIVE_LOW 0`
- D sounds: set `BUZZER_PASSIVE 1`

## Power

- Demo: laptop USB, or a power bank into the USB base.
- Standalone: 5V and GND pins from a 5V supply rated 1A or more. Do not connect USB and an
  external 5V at the same time.
- Solar: panel, then charge module, battery, 5V boost converter, then the 5V pin.
  Never connect the panel directly to the board.

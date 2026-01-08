# KNOB HID API

This project is a fork of [eynsai's improved KNOB v2.1 firmware](https://github.com/eynsai/baseline-design-knob). It adds a a number of HID reports that can be sent to and received from the KNOB.

## Report overview

The following reports are available:

**Device --> Host**
- [Rotary dial movement](#rotary-dial-movement)
- [Button presses](#button-presses)

**Host --> Device**
- [Set configuration](#set-configuration)
  - Update rate
  - Deadzone
- [Set layer](#set-layer)
- [Set RGB](#set-rgb) of each button individually

## How to install

**From precompiled HEX**
1. Download the HEX file from the latest release.
1. Run `qmk flash '/path/to/hexfile.hex'`

**From source**

1. Install the [QMK CLI](https://docs.qmk.fm/cli)
1. Run `qmk setup` and answer yes when it asks to check out the `qmk_firmware` repository.
1. Copy or link the `/knob-hidapi` folder into `qmk_firmware/keyboards` (so that `knob.c` ends up in `qmk_firmware/keyboards/knob-hidapi/knob.c`.
1. Run `qmk flash --kb knob-hidapi --km via`

**After installation**

Load the JSON file `knob-hidapi/keymaps/via/via.json` in VIA ([see these instructions](https://github.com/BaselineDesign/BaselineDesign-Knob/tree/main/KNOB_V2_QMK/knobv2#using-knob-v2-with-via)).

## Reports

All reports are sent and received on the following channel. This channel is shared with VIA, so make sure to check the report IDs that you send and receive.

- VID: 0x4244
- PID: 0x4b4e
- Usage page: 0xFF60
- Usage: 0x0061

Reports must be 32 bytes long; pad to length with zeroes.

**Type notation**

The text `(int16le)` means:
- **int**: signed integer type
- **16**: 16 bits (2 bytes)
- **le**: little-endian (**be** would be big-endian)

The text `(uint8:1)` means:
- **uint**: unsigned integer type
- **:1**: this is just one bit of the type

Multiple bits can occupy the same underlying type, and they're always right-aligned. Any undefined bits are reserved.


### Rotary dial movement

**Device --> Host**

This report is sent by the device while the rotary dial moves. How often the message is sent can be controlled by the update rate setting.

     ________ ________ ________ ________ ________ ________ ________ ________ ________ ________ ________ ... ________
    |  0xa1  |  0x01  |.......1|  0x13  `  0x00  |.......1|  0x00  `  0x00  `  0x98  `  0x3b  |   22 padding bytes  |
       ____     ____          .    ___________           .     ___________________________
        |        |            |         |                |                   \
        |        |            |         |                 \                    (float32le) Processed delta
        |        |            |          \                  (uint8:1) Processed clockwise indicator
        |        |             \           (int16le) Raw delta
        |         \              (uint8:1) Raw clockwise indicator
         \          (uint8) Current layer
           (uint8) Report ID: 0xa1

- **(uint8) Report ID**: Always 0xa1
- **(uint8) Current layer**: The currently active layer on the device. If multiple layers are active, this is the highest one.
- **(uint8:1) Raw clockwise indicator**: If 1, the dial moved clockwise. Otherwise the dial moved counterclockwise.
- **(int16le) Raw delta**: The dial delta value before any processing.
  - 16-bit signed little-endian integer.
  - The range is between -2048 (½ revolution counterclockwise) and 2047 (½ revolution clockwise).
- **(uint8:1) Processed clockwise indicator**
  - Opposite of raw indicator if the dial is set to reverse.
- **(float32le) Processed delta**: The dial delta value after processing.
  - 32-bit little-endian float. A value of 1.0 corresponds to one full rotation when sensitivity is set to 50% and acceleration is off.
  - Processing includes:
    - Reverse direction
    - Sensitivity
    - Acceleration

### Button presses

**Device --> Host**

This report is sent whenever a button is pressed or released.

     ________ ________ ________ ________ ________ ________ ... ________ 
    |  0xa2  |  0x01  |.......1|  0x02  |.....011|   27 padding bytes  |
       ____     ____          .   ____        ...
        |        |            |    |          || \ 
        |        |            |    |          | \  (bit) Right button state
        |        |            |    |           \  (bit) Middle button state
        |        |            |     \            (bit) Left button state
        |        |             \      (uint8) Button ID
        |         \              (bit) Press indicator
         \          (uint8) Current layer
           (uint8) Report ID: 0xa2

- **(uint8) Report ID**: Always 0xa2
- **(uint8) Current layer**: The currently active layer on the device. If multiple layers are active, this is the highest one.
- **(uint8:1) Press indicator**: If 1, the triggering button was pressed. Otherwise it was released.
- **(uint8) Button ID**: The ID of the button that triggered this event (0-2).
  - Buttons left, middle and right have IDs 0, 1 and 2 respectively.
- **(uint8:1) Left/Middle/Right button state**: Current state of each button. A 1 indicates that the button is pressed, a 0 indicates that it's not.

### Set configuration

**Host --> Device**

Send this report to configure the device. The configuration only persists while the device has power.

     ________ ________ ________ ________ ________ ________ ________ ________ ________ ... ________ 
    |  0xb1  |......11|reserved|reserved|  0x32  `  0x00  |  0xa0  `  0x00  |   24 padding bytes  |
       ____         ..                      ___________       ___________
        |           ||                           |                  \
        |           ||                            \                   (ushort16le)  Throttle value
        |           | \                             (ushort16le) Deadzone value
        |            \  (bit) Set deazone
         \             (bit) Set throttle
           (uint8) Report ID: 0xb1

- **(uint8) Report ID**: Always 0xb1
- **(uint8:1) Set throttle/deadzone**: A 1 indicates that the throtte/deadzone value should be set.
- **(ushort16le) Deadzone value**: How much the dial has to move before a report is sent, in 4096ths of a revolution.
  - A value of 0 triggers events if you so much as breathe on the dial. 10 is a good balance.
- **(ushort16le) Throttle value**: Delay in milliseconds between events while the dial turns. 10 is the minimum value.

### Set layer

**Host --> Device**

Send this report to change the active layer on the device.

     ________ ________ ________ ... ________ 
    |  0xb2  |  0x01  |   30 padding bytes  |
       ____     ____
        |         \
         \          (uint8) Target layer
           (uint8) Report ID: 0xb2

- **(uint8) Report ID**: Always 0xb2
- **(uint8) Target layer**: The layer to change to.

### Set RGB

**Host --> Device**

Send this report to set the red, green and blue components of each LED on the device. This only works when lighting is set to static in VIA; any animation will immediately override the color.

     ________ ________ ________ ________ ________ ________ ________ ________ ________ ________ ________ ________ ... ________
    |  0xb3  |.....111|  0xff  |  0x00  |  0x41  |  0xd5  |  0x00  |  0xff  |  0x2a  |  0x00  |  0xff  |   21 padding bytes  |
       ____        ...   _____    ____     ____     ____     ____     ____     ____     ____     ____
        |          |||     |       |        |        |        |        |        |        |        \
        |          |||     |       |        |        |        |        |        |         \         (uint8) Right blue
        |          |||     |       |        |        |        |        |         \          (uint8) Right green
        |          |||     |       |        |        |        |         \          (uint8) Right red
        |          |||     |       |        |        |         \          (uint8) Middle blue
        |          |||     |       |        |         \          (uint8) Middle green
        |          |||     |       |         \          (uint8) Middle red
        |          |||     |        \          (uint8) Left blue
        |          |||      \         (uint8) Left green
        |          || \       (uint8) Left red
        |          | \  (bit) Set right LED
        |          \   (bit) Set middle LED
         \           (bit) Set left LED
           (uint8) Report ID: 0xb3

- **(uint8) Report ID**: Always 0xb3
- **(uint8:1) Set left/middle/right LED**: Set the LED on 1, otherwise leave it as it was.
- **(uint8) Left/Middle/Right red/gree/blue**: The value (0-255) for the red/green/blue component of the left/middle/right LED.

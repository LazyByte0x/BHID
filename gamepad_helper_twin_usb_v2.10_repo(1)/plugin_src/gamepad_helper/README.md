# GamePad Helper - DragonRise Twin USB

This build is a fixed DragonRise Twin USB HID adapter build for GoldHEN.

## Target device

- VID: `0x0810`
- PID: `0x0001`
- HID Report ID `0x01`: Player 1
- HID Report ID `0x02`: Player 2

## Fixed mapping

The HID layout is the mapping recovered from the target DragonRise Twin USB adapter:

- `byte[1]` RX
- `byte[2]` RY
- `byte[3]` LX
- `byte[4]` LY
- `byte[5]` face buttons + D-pad
- `byte[6]` L1/R1/L2/R2/TouchPad/Options/L3/R3

The verified byte[6] mapping is unchanged.

## Input compatibility

- Fixed stick deadzone: `13` for both sticks.
- L2/R2 remain digital buttons and are also exposed as `analogButtons` at `0xFF` while pressed.
- No rumble is generated for Twin-backed handles.
- Normal controller handles continue through the original `libScePad` path.

## Player routing

The plugin supports both ways games commonly obtain pad handles:

- `scePadOpen()` index `0` -> Player 1
- `scePadOpen()` index `1` -> Player 2
- `scePadGetHandle()` index `0` -> Player 1
- `scePadGetHandle()` index `1` -> Player 2

` scePadClose()` removes the corresponding handle binding so stale handles are not reused.

## Compatibility hooks

In addition to `scePadRead` / `scePadReadState`, this build hooks:

- `scePadOpen`
- `scePadClose`
- `scePadGetHandle`
- `scePadGetControllerInformation`
- `scePadSetVibration`

The controller-information hook preserves platform metadata when available while exposing the fixed deadzone, standard controller class, and current Twin connection state.

## USB behavior

The input thread uses the HID interrupt IN endpoint discovered from the device descriptor. Transfer timeouts are treated as normal polling; fatal USB errors stop the thread and mark both Twin pads disconnected instead of spinning forever.

## Removed

- `/data/GoldHEN/gamepad.ini`
- `config.c`
- `config.h`
- Runtime custom mapping/config parsing

## Build

The source layout is compatible with the GoldHEN Plugins Repository build system. Your existing Windows BAT that builds `plugin_src/gamepad_helper/source/*.c` does not need to add any `common/*.c` files for this project.

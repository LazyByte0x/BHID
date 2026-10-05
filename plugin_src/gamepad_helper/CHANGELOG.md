# Twin USB GamePad Helper - Changes

## 2.10

- Fixed a major routing bug: Twin input is now returned only for handles explicitly bound to Player 1 or Player 2.
- Added `scePadGetHandle` hook so games that obtain handles without `scePadOpen` are supported by the same P1/P2 routing.
- Added `scePadClose` hook and explicit unbind support to prevent stale handle mappings.
- Added `scePadGetControllerInformation` hook with Twin connection state, standard device class, and fixed deadzone metadata.
- Limited vibration suppression to Twin-backed handles only; unrelated controllers keep the normal vibration path.
- Fixed short/invalid HID reports being accepted as valid controller state.
- Added explicit timeout handling for the USB interrupt thread.
- Fatal USB transfer errors now mark both pads disconnected and stop the read thread instead of retrying forever.
- L2/R2 now also expose full analog trigger pressure (`0xFF`) while pressed.
- Kept the verified DragonRise byte mapping unchanged.

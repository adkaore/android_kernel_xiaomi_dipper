# PMI8998 haptics — offline candidate, not hardware-validated

This is additional kernel work that can be done without the phone or a vendor
change. It is NOT evidence that every old/new diff has now been ported. The
board node stays disabled until a controlled crDroid hardware test is approved.
No device command, calibration extraction or PMIC write was performed here.

## Provenance and limits

The original Caleb Connolly binding/driver import is retained in Git history.
The import was disabled by default and had no enabled board node. The rewritten
candidate retains its license/authorship and narrows the advertised support to
PMI8998, LRA, sine, internal buffer mode and FF_RUMBLE. It does not advertise
unsupported PM660/PMI8996, ERM, PWM/audio modes or custom pattern capabilities.
The pinned downstream GPL driver supplies the register/trim reference; exact
source/archive/patch pins are in haptics-sources.json.

The old dipper board supplies the period 4878 us and voltage ceiling 3100 mV;
its PMIC parent supplies the 800 mA current limit, debounce 8, QWD mode, high-Z
option 1 and calibration period 4. Downstream has no brake-pattern property on
this board, so its zero-initialized four entries are retained, not the generic
candidate's invented 3/3/2/1 pattern. Old "overdrive" does not permit setting the
VMAX override bit on PMI8998: the old code restricts that bit to PM660.

This candidate deliberately uses one repeating rumble buffer. The three Xiaomi
custom patterns, duration-dependent direct/buffer auto mode and adaptive drive
period tracking are NOT ported or claimed equivalent. Android vibration effects
and perceived intensity/latency need later kernel/hardware work too, not merely
an Android HAL. New rumble must not be described as full legacy haptics parity.

## Corrections to the generic candidate

- The memless callback only updates the gated request and queues work under a
  spinlock. No sleeping register access occurs in the input event callback.
- Locks and both work items are initialized before cleanup can run. Failure,
  close, suspend, unbind and shutdown close the gate, drain both workers and stop
  synchronously. There is no post-close queued stop, manual double-unregister or
  destroyed mutex used by an input close callback.
- All PMIC accesses are serialized, including secure unlock plus TEST2 writes.
  Stop attempts PLAY clear, module disable and autoresonance disable even when
  an earlier operation fails. Errors are returned/logged, not called success.
- Strength scales over the full unsigned 16-bit FF range. Quantization cannot
  exceed the board voltage ceiling. The upstream candidate divided an 8-bit
  magnitude by 100 and used a generic maximum instead of the board ceiling.
- Debounce cycles are logarithmically encoded (8/16/32 become 1/2/3), not written
  as 32 into a three-bit field. Brake cells are read as u32, matching the binding.
- Auto-resonance starts after the QWD back-EMF interval in separate delayed work,
  not a sleep that prolongs short effects. Active status is polled every 20 ms;
  bus, short and autoresonance errors latch the driver off until rebind. This
  conservative policy is not the old retry/adaptive-frequency algorithm.
- PMIC subtype and USID parity are checked before any write. Another PMIC is not
  accepted merely because its DT contains the generic fallback compatible.

## Factory RC oscillator trim

The old driver reads MISC offset 0xf3 on PMI8998's base USID and adjusts the drive
period by the sign and 0.7-percent steps. Ignoring that byte loses a real part of
old-kernel behavior. A new tiny NVMEM provider exposes **only that one byte** at
physical PMIC address 0x9f3. It has no write operation, checks the PMI8998 subtype
and base USID, and cannot expose arbitrary PMIC registers. The consumer gets
its byte through a fixed-layout NVMEM cell; missing/deferred/read-failed trim is
not silently replaced by zero or another device's value. Corrected periods
outside the 12-bit range are rejected rather than truncated.

The old whole-MISC vendor accessor is not imported, nor is MISC mislabeled as
SDAM storage. The provider is available on all common-board candidates, while
the haptics node remains disabled. No factory calibration values are committed.

## Checks and actual Android boundary

Both objects compile with W=1 and -Werror. The common board is schema-checked
with the new bindings. Host ASan/UBSan tests execute the actual C encoders over
every supported period and all trim bytes, all FF magnitudes at the dipper
ceiling and boundary magnitudes at every supported voltage ceiling. Source
contracts check callback/teardown ordering; compiled DT checks confirm the
source-derived values, base/secondary USIDs and NVMEM connection. These do not
execute Linux IRQ/workqueue failure paths or validate a vibrating actuator.

The old Android service's LED/sysfs controls are not the FF input ABI. A matching
crDroid vibrator HAL/service will eventually be needed. No fake old sysfs files
or dummy capabilities were added to hide that difference. This does not block
continuing the independent kernel work or mean other subsystems are complete.

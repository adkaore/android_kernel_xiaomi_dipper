# PMI8998 watchdog supervision candidate

Target remains crDroid 13.1 / Android 17. No phone access or physical charging
test has occurred. This completes a bounded native software supervision path,
not full electrical/JEITA/thermal, PMIC failure or target-health validation.

## Evidence and scope

The pinned Qualcomm `1f01db5a42ccd3123517a9d5730024719abf095d`, official Xiaomi
`dcfb1676925aec83b06ae04fd8ca989ced75067c` and legacy crDroid
`0b3e38ef6c40e0a346dfa0241aeceaf5540e91f6` agree in `qpnp-smb2.c` on default
bark 64 seconds and bite 8 seconds. They enable bark IRQ and plugin-controlled
timing, and preserve the independent SNARL field. Xiaomi's IRQ table marks bark
as a wake source. No dipper watchdog-time override was found in the checked
board includes. Register assignments are checked against all three `smb-reg.h`
copies by the source tests. Existing Qualcomm/Xiaomi copyright is retained.

The new path is selected only after PMI8998 subtype, base 0x1000 and even base
USID verification. PM660 does not silently acquire this policy. Its existing
code is not claimed audited or needed for dipper.

## Ordered control

1. Validate battery float ceiling and command software-controlled charging OFF.
2. Stop both manual/plugin watchdog activation and bark IRQ while programming
   the checked timeout fields; do not zero the independent SNARL field.
3. Complete charger limits, IRQ allocation and wakeup setup.
4. Pet the correct charger-relative 0x1643 register, enable plugin supervision,
   then perform the final charging enable under the policy mutex.

Failed configure/start writes abort probe with charging still gated. A runtime
pet failure latches a fault, clears readiness and attempts both charging-disable
steps. A confirmed software OFF permits stopping the faulting watchdog, avoiding
repeated fault wakeups; a cleanup error is logged rather than called hardware
OFF. HEALTH reports the known supervision failure, not GOOD, and later writes
cannot silently restart charging. Explicit reprobe is needed for recovery.
Removal attempts charging and watchdog shutdown and reports errors separately.

The copied bite-action bit follows the pinned BSP value. This is NOT proof of
an autonomous fail-safe battery cutoff if the CPU or bus hangs. Such claims
require PMIC documentation and physical fault testing, not a register name.

## Suspend/wake

The regular managed wake IRQ remains the USB plugin IRQ. System sleep separately
arms bark via `enable_irq_wake`, balancing it on resume and managed release.
Arming errors abort suspend. Disabling the device's wakeup policy while watchdog
supervision is active blocks suspend instead of silently disabling supervision.
This conservative behavior is explicit and needs real suspend/battery testing.
It is not an acceptable reason to claim optimal standby power already.

## Offline checks

ARM64 QEMU executes the real configuration/start/stop/service methods over an
uncached error-injecting regmap. Cases cover preserved SNARL bits, proper pet
address, start errors, stop errors, latched pet failure, cleanup failure, disabled
readiness and other-PMIC exclusion. The no-wakeup suspend rejection is exercised
with PM/suspend compiled in. A virtual machine does NOT prove the real SPMI
interrupt can wake the phone; successful physical bark IRQ arming, timing and
waveforms remain unvalidated. Existing charger/Type-C tests still run too.

# Native SMB2 safety corrections from the source comparison

Target remains crDroid 13.1 / Android 17. No phone was accessed or flashed.
This is not full downstream charger, JEITA, parallel-charge or PD policy parity.

## Input programming ceiling, not permission to draw more

Official Xiaomi dcfb1676925aec83b06ae04fd8ca989ced75067c reduces the Qualcomm
base's usb_icl configuration ceiling from 4.8 A to 3 A in v1_params. The old
crDroid comparison retains this. Its later fix by awkoo,
139347ae71d25ca04d0d249d97d71cb476a6c544, bounds requested ICL before programming.
The native adaptation fc4189328965d33dfcb454eeec462c20b26e5c06 preserves awkoo's
authorship and identifies the original commit. It does not copy SMB5 or voters.

The native driver accepts an immutable input-current-limit-microamp constraint,
validated before hardware init and power-supply publication. All dipper candidates
set 3000000. The old 4800000 register ceiling remains the default for descriptions
without a board constraint; we do not call that default a Mi 8 factory limit.
Negative signed power_supply requests stay invalid rather than becoming a large
unsigned request which would clamp to a dangerously positive value. Valid requests
are capped to the board limit and quantized downward to 25 mA steps. Invalid
ceiling ranges/steps fail probe instead of being silently ignored.

This ceiling does not set the initial current to 3 A, increase the existing 1 A
fast-charge cap, negotiate a source contract, or control USB input suspension.
It bounds writes requested through the native setter, including charger detection
and power_supply. It does not implement the old <=25 mA suspend policy or its
INT_MAX/unlimited-vote convention. Raw ICL status is still hardware telemetry,
not a value clamped to make the displayed status look like the configured limit.

The source comparison also identified factory float/fast-charge/OTG limits;
those need their own parameter/revision and startup-policy review. In particular,
no higher Qualcomm limit is accepted merely because it exists in the base tree.

## Correct watchdog register address

The native driver formerly wrote the relative offset 0x643 without adding the
charger base. Xiaomi and the declared Qualcomm smb-reg.h define the pet register
as MISC_BASE + 0x43: on this PMIC, 0x1643. The driver now uses its actual base
plus the existing offset, just as its other charger register accesses do.
This corrects an address calculation; it does not disable the watchdog, extend
its safety timers, prove a pet reached hardware or alter fault-recovery policy.

## Checks and open work

The shared real ICL encoder is executed under ASan/UBSan: every integer request
from 0 to 5000000 at both the factory and default ceilings, negative extremes,
invalid ceiling encodings, and boundaries at every valid ceiling. Compiled-DTB
tests check the 3 A ceiling across all 14 selected candidates. These are source,
math and wiring checks, not electrical or Linux-concurrency emulation.

Basic fixed-5-V TCPM input/current coordination is now in input-handoff.md.
Higher-voltage policy and physical confirmation,
JEITA/thermal/parallel charging, gauge profiles, startup/suspend/fault behavior,
measurement and the actual crDroid 13.1 health/USB product remain separate work.
Do not enable the new Type-C/PD nodes based on this current bound alone.

## Float voltage and honest status reporting

The native float encoder previously added one after division, unlike the pinned
Qualcomm/Xiaomi parameter encoder. For the dipper 4400000 uV battery design
ceiling this programmed code 122 (4402500 uV), above the requested ceiling.
The shared encoder now validates the qualified register range and rounds down:
code 121 (4395000 uV). Missing/negative and out-of-range battery design values
are rejected, not wrapped into the byte register. This does not validate the
actual battery/profile or the physical float voltage.

Health decoding now treats STATUS_2 as flags rather than matching the whole byte.
For example an input-limited bit cannot mask a simultaneous overvoltage or hot
condition. Timer expiry and missing termination are faults, not GOOD or an
invented battery-absence reading; conflicting thermal comparator flags are an
unspecified fault. The charger STATUS getter also respects hard blocking flags.
All 256 status bytes are exercised by the real shared decoder under sanitizers;
the eight bit assignments are checked against all three pinned hardware sources.

USBIN IIO voltage/current reads no longer return a synthetic zero merely because
charging stopped or a separate status read failed. They return the ADC result
or its error. The OV IRQ path checks register-read success before inspecting the
status word, and notifies power_supply even when the read is unavailable.

## Startup and teardown are part of the safety path

Battery float validation now precedes any charger reconfiguration. The published
power_supply returns EAGAIN until setup completes; a mutex serializes public
properties and detection against shutdown. Valid setup first commands charging
OFF and selects software control, then applies the float ceiling and remaining
limits. The init table no longer enables charging halfway through. IRQs and
wakeup are prepared before the final enable; wakeup-setup failures are checked.

Managed release order is IRQ/wake producers, ready gate and charging OFF, delayed
work drain, then power_supply unregister. A setup failure after hardware changes
also runs the OFF action. Both command-disable and source-selection writes are
attempted even if one fails; shutdown I/O errors are logged, not relabeled proof
that hardware stopped. Software sequencing/source assertions are not injected
hardware-failure or concurrency tests. The OV/thermal protections are not bypassed.
The inherited watchdog-disable entries are now skipped for verified PMI8998;
the separately ordered supervision and wake path is in watchdog.md. It has
error-injected kernel tests, not a proof of physical wake or automatic cutoff.

## Bounded detection instead of an indefinite wake/poll loop

APSD now recognizes exact documented result combinations. Zero, floating and
conflicting results remain USB_TYPE_UNKNOWN instead of being mislabeled SDP or
DCP. A DCP result with QC bits is not a negotiated high-voltage contract; HVDCP
still stays disabled. Unknown/unready sources only lower an existing input
configuration toward 100 mA (or the lower board ceiling), preserving a lower or
zero programmed limit instead of raising it. I/O failures are propagated/logged,
not used as synthetic ADC measurements or successful current writes.

A new plug event resets a bounded eight-read budget. Detection is rerun once,
not on every poll. Subsequent checks are delayed work, with no polling sleeps
while holding the policy mutex. Completion, timeout, disconnect, I/O error and
teardown release the detection wakeup activation; only a queued retry retains
it. This prevents suspend from silently interrupting the bounded detection work
without leaving a perpetual wake lock on an unrecognized charger. A new plug
reschedules the debounce rather than inheriting an old delayed-work deadline.

The eight-read/one-second polling policy is a development bound, not proof of
an electrical timing requirement. Exact source classification, input limits,
suspend/wake and fault paths still need controlled hardware validation. Full
TCPM negotiation/input suspension and arbitration with userspace are not claimed.

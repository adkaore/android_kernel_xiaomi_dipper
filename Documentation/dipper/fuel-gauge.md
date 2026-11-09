# PMI8998 fuel gauge: direct telemetry, not profile management

The initial Mobian qcom_fg reference combines older gauges with GEN3, initializes
IMA/DMA state and writes JEITA thresholds. Its GEN3 current/voltage functions
also leave digital revision 1 byte order unimplemented. Importing that code
unchanged would not be a safe way to preserve this phone's gauge state.

The new native driver reads only the PMI8998 direct register interface, using
these pinned primary sources:

- Legacy commit 0b3e38ef6c40e0a346dfa0241aeceaf5540e91f6,
  drivers/power/supply/qcom/qpnp-fg-gen3.c and fg-reg.h.
- The same commit, include/linux/qpnp/qpnp-revid.h (revision constants), and
  the existing PMI8998 device tree (SPMI base and IRQ mapping).
- The current ACK Documentation/ABI/testing/sysfs-class-power for units and
  current sign, rather than copying a conflicting old vendor convention.

No SRAM/IMA/DMA access, reset, profile/learning/calibration update, JEITA write,
charger-limit change, background polling or firmware operation is implemented.
The existing running algorithm and correct battery profile are prerequisites.
The candidate board node remains disabled until this state is checked in a
controlled crDroid test. The phone has not been accessed.

## What is implemented

- Runtime PMIC subtype/base-USID verification and digital major 1/2 selection.
  Revision 1 voltage/current words are big-endian; revision 2 words are
  little-endian. Unknown revisions are rejected, not guessed from SoC IDs.
- Battery presence from the actual hardware missing bit, not constant true.
- SOC algorithm-enable/ready checks. Missing/unready data returns an error,
  not a made-up 50-percent reading. Primary/copy SOC pairs get five bounded
  attempts and then EAGAIN, not a silently chosen minimum value.
- SOC endpoints 0 and 100 are explicit; interior codes scale to 1 through 99.
  Xiaomi's dynamic full/maintenance-SOC policy is not duplicated here.
- Live voltage, signed current, quarter-kelvin temperature. Current is converted
  to the modern power_supply convention: positive charging, negative discharge.
  Temperature retains quarter-kelvin information and uses the exact Celsius
  offset, rather than the old whole-kelvin rounding and 273 approximation.
- Charge STATUS comes from the linked charger, not a heuristic based on current,
  USB presence or SOC 100. Supplier errors are propagated.
- SOC ready/delta, missing-battery and temperature IRQs notify power_supply.
  Managed IRQs are released before the managed supply. There are no driver work
  items, timers, notifier registrations or cached measurements to outlive it.

The new power_supply is pmi8998-bms. Matching crDroid health integration must not
double-invert CURRENT_NOW to compensate for the old vendor sign convention.
That is a real userspace boundary, not a reason to stop other kernel work.

## What is not solved

Readiness does not prove the battery model/profile is correct, prove a cold boot
initialized it, or replace profile-integrity and battery-ID checking. This driver
intentionally cannot repair or initialize a gauge. Coulomb counters, learned full
capacity, cycle count, profile programming, ESR/adaptive coefficients and the old
software SOC maintenance algorithm remain separate **kernel** work. They are not
all relabeled as vendor dependencies. Hardware charging/JEITA safety and actual
battery characteristics have not been validated by these offline checks.

## Offline checks

The core fragment selects the module. The new binding is linked through the
PMIC parent's constrained oneOf, without weakening the existing BMS binding.
All common-board targets describe identical gauge wiring and preserve their
separate boot selectors. The direct helper encoders are exercised under host
ASan/UBSan for all raw voltage/current words, both byte orders, all SOC codes,
all temperature codes and reserved temperature bits. Source contracts forbid
write paths and check readiness, bounded retry, supplier status and devres order.
These checks do not emulate a gauge or Linux IRQ failure paths.

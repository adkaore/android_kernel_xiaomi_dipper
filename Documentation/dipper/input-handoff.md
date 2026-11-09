# Native PMI8998 input / Type-C handoff (fixed 5 V candidate)

Target is crDroid 13.1 / Android 17. Both new Type-C and boost nodes remain
disabled; no connector PDOs/identity or role graph were invented. No phone was
accessed. This is native kernel coordination, not a claim about the target HAL.

## One policy owner

The Type-C node references the same-PMIC SMB2 power supply. Probe verifies the
supplier's parent/compatible and ready state, takes a managed supply reference,
and creates a consumer/supplier device link for removal and suspend ordering.
The PD PHY base is checked against 0x1700 before hardware setup for this PMIC.

If the Type-C sibling is enabled, SMB2 starts with USB input suspended and does
not let APSD override TCPM's current decisions. In that mode CURRENT_MAX is
read-only in sysfs; kernel power_supply setters still carry TCPM requests. ONLINE
also stays read-only in sysfs. Without an enabled sibling, existing bounded APSD
and its ordinary CURRENT_MAX control remain in use. A failed/missing Type-C
consumer does not silently fall back to a competing source/sink policy.

## Handoff and failure ordering

The kernel-only ONLINE setter writes the real USBIN suspend command (relative
0x340). Suspending input must be acknowledged by POWER_PATH_STATUS's USBIN
suspend bit before success is returned. The poll is bounded to 100 ms; a timeout
or bus error is not treated as proof that input was isolated. The timeout is a
development guard, not an electrical timing guarantee, and must be checked on
hardware (including an unplugged port).

Source enable first requests and confirms input suspension, then enables the
boost regulator. Input-suspend failure prevents boost; boost failure leaves
input suspended for explicit TCPM recovery. Sink resume first disables boost;
a failed disable prevents input resume. Simultaneous source and sink requests
are rejected. Teardown attempts both boost OFF and input suspend even if another
cleanup step fails, retaining/logging errors rather than claiming hardware OFF.

The Type-C mutex calls into the charger's mutex in one direction only. The
charger never calls back into Type-C while holding its policy mutex; supply
notifications are queued. These source/lifetime contracts are not a proof of
all possible Linux concurrency or physical rail timing.

## No unsupported PD voltage/capabilities

The current callback programs the native signed/board-bounded input limit. Only
fixed 5 V PDOs with nonzero current within the boost/sink ceilings are accepted
before TCPM publication. PPS, battery/variable PDOs, higher voltage and unsupported
FRS/unchunked-extended flags are rejected, not advertised and then silently
ignored. The existing 1 A battery fast-charge cap is not raised. The factory's
9 V limit is evidence for further work, not implemented 9 V support.

A later implementation must add and check adapter allowance, switching frequency,
thermal/JEITA and battery-profile coordination before expanding that gate. Actual
VBUS measurement/vSafe0V, revision-specific CC/PBS workarounds, full fault and
role-swap behavior, wake timing and the target crDroid health/USB product remain
open. Do not enable this candidate just because module and decoder tests pass.

## Offline checks

The real PDO guard is executed under ASan/UBSan over all four PDO types, every
voltage/current field value and both the conservative source and factory sink
ceilings. Additional boundary cases reject unsupported capability flags. The
combined Type-C helper suite covers 41,943,040 combinations; this is NOT TCPM,
regmap-failure, electrical or hardware emulation. Compiled-DTB tests check the
supplier link and preserve disabled status on all 14 selected board candidates.

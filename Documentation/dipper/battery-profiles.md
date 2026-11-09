# Resolved battery profiles and thermal policy

Target: crDroid 13.1 / Android 17. These are hardware comparison sources, not
target HAL requirements. No owner's battery ID, calibration or loaded SRAM was
read. A successful source audit is NOT profile programming or JEITA parity.

## Reproduce the evidence

First run audit-legacy-boards.py for both pinned sources into separate external
directories, with the pinned clang, built dtc and schema Python as described in
hardware-comparison.md. Then, using that Python:

    tools/dipper/audit-battery-profiles.py --xiaomi-dir /home/adkaore/android/work/xiaomi-board-audit-recheck --legacy-dir /home/adkaore/android/work/legacy-board-audit-recheck --output /home/adkaore/android/work/battery-policy-comparison.json

The tool checks source stamps/manifest coverage and records input blob hashes.
Its output is committed as battery-policy-comparison.json. There are 15 source
definitions per source (including the unlisted revision-1 P0 and two plugins),
not 15 supported final phone boards. Plugins retain unresolved external fixups;
they are not applied to an old base or declared compatible with the new base.
Three result groups differ in standalone/plugin node paths, not battery data.

## Two public factory profiles, no automatic selection for this phone

Both Xiaomi and legacy crDroid contain byte-identical E1 Coslight and ATL
profiles. Each has 224 payload bytes. Their SHA256 identities are in the report;
the declared GUI checksum is preserved but its algorithm is not verified. A
matching SHA256 of source payloads is not a checksum of the phone's loaded SRAM.
The declared Qualcomm base does not contain these two Xiaomi profile files;
that absence does not make the profiles irrelevant.

Both profiles specify 3400 mAh, 4.4 V maximum and 3.3 A maximum fast-charge
current. Nominal battery-ID resistors are 100 kohm (Coslight) and 68 kohm (ATL).
The old caller floors measured ohms to integer kohm and allows a 15-percent
nominal tolerance: 85..115 and 58..78 integer kohm respectively. Source selection
tests cover boundaries, unmatched IDs and ambiguous input. None selects the
owner's actual battery or licenses using a profile for an unidentified replacement.

## JEITA, cycle aging and thermal current are different policies

The profiles agree on temperature-dependent current/voltage ranges. In the
0..50 deci-Celsius range the specified current is only 330000 uA; a generic 1 A
cap alone is therefore NOT a replacement for the factory temperature policy.
The warm 451..600 range reduces float voltage to 4100000 uV. Gauge hardware
thresholds are 0/15/45/60 degrees with 2-degree hysteresis in the resolved board.
The exact software hysteresis and out-of-range behavior require a reviewed
native implementation, not copying the old get_val() and voter interfaces.

`dynamic-fv-ranges` uses battery CYCLE_COUNT, not temperature; this is checked
against handle_dynamic_fv() in the pinned source. Learning/cycle persistence
must be correct before that policy can be meaningfully transferred.

The QC2 thermal input-current array contains 10255000 uA at zero-based index 10
in BOTH source trees, between 1125000 and 925000. It exceeds both the configured
3 A input and 3.3 A battery-current limits and breaks the decreasing sequence.
The report preserves it and flags it; it neither imports it nor guesses a fix.
1025000 is a plausible intended value, not a verified Xiaomi correction.

This entry is an input-current vote, not a measurement or an unconditional
hardware command: smb-lib.c submits it to USB_ICL, battery.c creates a VOTE_MIN
election, and the parameter writer rejects out-of-range values. Other votes and
charger state determine the effective request. The bad entry can weaken that
thermal level; source inspection alone does not show it caused 10 A draw.

## Explicit remaining implementation dependencies

The old GEN3 loader reads battery ID, checks integrity flags and a profile
prefix, may modify RCONN fields, clears learning/cycle state on reload, writes
224 bytes through the IMA/SRAM transaction protocol, marks integrity and restarts
the SOC algorithm. Those are state-changing hardware operations, not a memcpy
which can safely be added to the read-only telemetry driver.

Native SRAM/IMA ownership, failed-transaction recovery, verified integrity/model,
battery-ID lifecycle, cold boot, learning/cycle persistence and charge-policy
coordination remain kernel implementation work. They are not implemented by
this inventory and are not all vendor blockers. No profile write, force reload,
gauge restart, extra charging current or guessed thermal replacement is enabled.

# Hardware comparisons: Xiaomi, old crDroid, Linux and Qualcomm

Target remains **crDroid 13.1 / Android 17 / ACK android17-6.18**. None of the
older trees specifies the target HAL/vendor ABI. No phone was accessed.

## Pinned, independent sources

| Role | Revision | Linux |
|---|---|---|
| Official MiCode dipper-q-oss | dcfb1676925aec83b06ae04fd8ca989ced75067c | 4.9.186 |
| Old crDroid comparison, 15.0 | 0b3e38ef6c40e0a346dfa0241aeceaf5540e91f6 | 4.9.337 |
| Xiaomi-declared Qualcomm tag LA.UM.8.3.r1-05800-sdm845.0 | 1f01db5a42ccd3123517a9d5730024719abf095d | 4.9.186 |
| Linux stable v4.9.186 control | 35c308d7828de5c5784bb75efcf848996b3a82d1 | 4.9.186 |
| Linux stable v4.9.337 control | 87a72e81764d2fc5411706c551462d61cdb97660 | 4.9.337 |

Repositories, annotated tag objects and commit pins are in sources.json. MiCode
README revision 76ab390d9bcd85e505ae33043621095084d56ac8 declares the Qualcomm
tag for this multi-device Q branch. Its exact inspected bytes are hashed in
hardware-source-provenance.json. This declaration plus a resolved tag does not
prove an exact Git merge base. The old crDroid tree's exact Qualcomm kernel base
remains **unresolved** in the bounded history inspected here. Its WLAN component
merge tags do not identify the whole kernel's BSP base. Tag signatures were not
cryptographically verified. The crDroid tip is still not proven to match the
11.17 release ZIP exactly; it is our pinned comparison source, not a new claim.

## Method: subsystem evidence, not a global patch extraction

hardware-comparison-scope.json lists 64 selected files in seven groups: power
and suspend/wake; charging/gauge; Type-C/PD; camera; board variants; NFC wiring;
and generic version controls. hardware-comparison.json records file hashes,
presence and six pairwise inventories, including Linux-to-Qualcomm and the
4.9.186-to-4.9.337 control. It intentionally does NOT classify every changed
line as a Mi 8 change, attribute every later difference to crDroid authors, or
produce patches to apply to 6.18. Linux alone lacks many BSP drivers; numerical
version matching is not ancestry. Byte equality is not hardware validation.

Before transfer, establish the setting/erratum and its device/revision scope,
check whether 6.18 already implements an equivalent, review later fixes and
power/error/lifetime behavior, preserve author attribution, and test the native
implementation. Absence in Xiaomi is not a deletion criterion. Generic security,
compiler, scheduler, Android and Qualcomm updates are not automatically board
patches. No KernelSU/security-policy changes were imported from the old tree.

## Reviewed findings and decisions

### Charging: factory ceilings differ from the Qualcomm base

The v1_params table in qpnp-smb2.c is a concrete Xiaomi configuration delta:

| Parameter | Declared Qualcomm base | Xiaomi Q and old crDroid |
|---|---:|---:|
| Fast-charge current maximum | 4.5 A | 3.3 A |
| Float-voltage maximum | 4.92 V | 4.4 V |
| USB input-current configuration maximum | 4.8 A | 3 A |
| OTG current maximum | 2 A | 1.5 A |

These are configured ceilings, not independent proof of absolute silicon limits.
The ICL *status* range remains 4.8 A; it must not be confused with the 3 A input
configuration ceiling. Existing native charger fast charge remains conservatively
limited to 1 A; this audit is not permission to raise it. JEITA compensation,
thermal tables, battery identification and parallel-charger behavior need their
own review, not transplantation as one battery/performance diff.

The bounded crDroid history contains a real later fix by awkoo:
139347ae71d25ca04d0d249d97d71cb476a6c544, capping requested input current to the
configured hardware maximum. It is absent from Xiaomi Q but relevant: preserve
its safety requirement when adapting the native charger, including signed-input
validation and quantization. Its downstream INT_MAX/unlimited-vote convention
is NOT automatically a valid native power_supply interface. Do not copy the
unrelated SMB5 half or claim an Android 17 charging policy from this fix.

### Type-C: shared registers, revision workarounds still required

qpnp-pdphy.c is byte-identical in the declared Qualcomm base, Xiaomi Q and our
old crDroid snapshot. smb-reg.h changes between Xiaomi and old crDroid only in
a copyright year; that is not a register-layout change. This supports the
register/IRQ comparison behind our separate PMI8998 backend, not PD electrical
or HAL compatibility. The PM8150B VBUS selector fallback remains inappropriate;
use the Xiaomi-constrained 250 mA-step range, not its different 500 mA encoding.

Xiaomi and old crDroid retain PMI8998 CC2-removal and PBS workarounds. Those
remain explicit kernel review items for the new backend. Later ufp-only-mode
support in the shared old driver is conditional and is not selected by dipper's
DTS files: do not silently force the whole port to sink-only because another
board needed that option. Current disabled candidates remain disabled pending
charging/input suspension, measured VBUS and role-graph work in typec.md.

### Suspend/wake: do not copy obsolete debug paths as power policy

The selected lpm-workarounds and msm-pm files are unchanged between Xiaomi and
old crDroid. Their presence is a Qualcomm/BSP reference, not a Xiaomi-only diff.
lpm-levels.c later uses cpu_do_idle in place of wfi and removes factory regulator,
GPIO and sleep-status dumps around suspend. Those differences are not evidence
that suspend levels or rail sequencing should be deleted. Reconcile the actual
errata and wake dependencies with 6.18 PSCI/RPMh; no battery-life or suspend
success is inferred and no old LPM core is transplanted.

### Board revisions: keep later ramoops

The source resolver compiled all 15 Xiaomi root definitions without forced dtc
output: 12 listed standalone targets, two plugins and one unlisted revision-1
P0, just like the old comparison. Board/SoC selectors and listed status match.
The resolved fixed reservations are identical **except** old crDroid adds
ramoops at b0000000, size 400000. Keep that later diagnostic reservation and
its verified subregion sizes; absence from Xiaomi is not a reason to remove it.
See xiaomi-resolved-boards.json and hardware-board-comparison.json. Plugins were
not applied to old MTP bases, and 15 source roots do not mean 15 tested phones.

### Camera: factory source is evidence, not invented capabilities

The official tip's subject is camera: fix new version. That subject alone does
not prove a sensor mode, EEPROM layout or source match for a later binary vendor.
Camera DTS revisions, power GPIO/regulator sequences, actuator, OIS and EEPROM
files are in the inventory. The selected camera GPIO helper's Xiaomi-to-crDroid
diff is a copyright update and redundant-parenthesis cleanup, not a new rail
sequence. Other camera deltas still need functional/revision-specific review.
No modes, IR video support, calibration values or all-camera HAL support were
invented or enabled by this comparison.

### NFC: hardware confirmation does not choose the target ABI

Xiaomi's old nq-nci wiring is an additional hardware reference, not an order to
preserve its character-device ABI. crdroid13-target-audit.md still controls the
unresolved target NFC decision; the generic NCI topic remains separate.

## Reproduce offline inventories

After fetching the exact pins in sources.json, run from the repository root:

    python3 /home/adkaore/android/android_kernel_xiaomi_dipper/tools/dipper/audit-hardware-sources.py > /home/adkaore/android/work/hardware-comparison.json

For the factory source resolver (requires the provisioned dtschema Python):

    /home/adkaore/android/toolchains/dtschema-venv/bin/python /home/adkaore/android/android_kernel_xiaomi_dipper/tools/dipper/audit-legacy-boards.py --source xiaomi --output-dir /home/adkaore/android/work/xiaomi-board-audit --clang /home/adkaore/android/toolchains/clang-r596125/bin/clang --dtc /home/adkaore/android/out/dipper-core/scripts/dtc/dtc

The default --source legacy preserves the old report format. Use a fresh output
directory for a changed pin. Reports and source tests do not emulate hardware,
IRQ races, electrical limits, charging policy or the target Android build.

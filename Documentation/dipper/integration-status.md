# Integration status — 2026-11-09

This is a development kernel, not a stable or flashable crDroid release.
No phone was accessed, rebooted, flashed or otherwise changed.

The exact target is crDroid 13.1 / Android 17.
Target requirements are tracked in crdroid13-target-audit.md/json. The platform
17.0 source is available, but a matching dipper Android 17 device/vendor
selection is not confirmed. The old 15.0 trees are comparison references only;
no NFC ABI is selected automatically from them or from the age of an interface.

Official Xiaomi dipper-q-oss and its declared Qualcomm base are now separate,
pinned comparison sources, with Linux version controls and subsystem evidence
in hardware-comparison.md. This does not change the target HAL uncertainty.

## Completed offline work

- Hardened EA8074 panel: LP command parity with the old kernel, complete vendor
  poweroff sequence, safe power/backlight serialization and reference lifetime.
- Shared v2/v2.1 DT: checked reservations, RPMh, storage, display, USB peripheral,
  Wi-Fi/Bluetooth, charger and video-codec descriptions. No shared calibration
  MAC or guessed live framebuffer is copied from another phone. The final
  downstream secure-memory, splash, ramdump and ramoops overrides are now
  recovered from resolved legacy DTS, with coverage regression tests.
- Twelve standalone board-selector candidates, plus two selection plugins on a
  matching modern common base. All listed old targets are mapped explicitly.
  The unlisted revision-1 P0 is documented, not falsely advertised as supported.
  See board-variants.md: old MTP plugin ABI and hardware parity are NOT implied.
- IMX363 candidate: constrained RAW mode, cleaned control/PM handling and binding.
  No enabled camera node or claim of AF/OIS/HDR/video-mode parity.
- New runtime-only FTM5 driver, not the firmware-updating downstream code.
  Compile-selected, with a deliberately disabled board node pending identification.
- PMI8998 LRA rumble candidate and read-only factory RC trim provider; corrected
  callback context, voltage scaling, debounce and teardown. Board motor node
  stays disabled. Custom pattern/auto-mode parity remains open; see haptics.md.
- Direct, read-only PMI8998 FG telemetry for PMIC digital revision 1/2, including
  byte order, hardware presence/readiness and SOC shadow checks. The node stays
  disabled pending profile-state validation. Full profile/learning management
  remains kernel work; see fuel-gauge.md.
- PMI8998 Type-C/PD and distinct boost-provider candidates are compile-selected,
  with disabled nodes and no guessed connector/role policy. CC decoding, PD RX
  bounds, reset signaling and producer lifetimes are hardened; see typec.md.
  Charger/PD input limits, VBUS measurement and revision workarounds remain open.
- Native charger input programming now respects the factory 3 A board ceiling,
  adapting the later crDroid ICL safeguard with original authorship preserved.
  Negative requests remain invalid and quantization never rounds up. The
  watchdog pet uses the charger-relative address; see charger-safety.md.
- Native charger startup validates/rounds float voltage down before reconfiguration,
  enables only after IRQ/wake setup, and gates/serializes callbacks on teardown.
  Fault flags and ADC errors are no longer converted to healthy/zero guesses.
  APSD uses exact classifications and a bounded wake-aware retry path; see
  charger-safety.md. This is not complete watchdog/PD/JEITA parity.
- Explicit fragment requests are checked after Kconfig; dependencies cannot
  silently drop a requested driver. Rust, CFI, SELinux and BTF remain enabled.
- Combined build produced Image, 14 final DTB targets and 151 current-build modules
  on a clean Git tree. The matching helper base and two plugins are hashed too.
  Historical result records identify their own source revisions.

## Repeat checks

Provision the pinned Google archives in toolchains.json and bindgen.json.
The schema venv package versions are in tools/dipper/dtschema-requirements.txt.
These tools are NOT hermetic and version pins alone are not signed provenance.
Then run:

    TOOLCHAINS_DIR=/home/adkaore/android/toolchains DT_SCHEMA_VENV=/home/adkaore/android/toolchains/dtschema-venv tools/dipper/verify-core.sh /home/adkaore/android/out/dipper-core

The verifier checks schema diagnostics, not just dt-validate's exit code. The
FTM5 C tests execute the real decoder under ASan/UBSan. Source assertions and
DTB parsing are not emulation of Linux IRQ or regulator failure paths.

## Remaining kernel work — do not relabel it all as a vendor problem

- Board/controller identification: the 15 old root files resolve to 12 listed
  standalone targets, two plugins and one unlisted revision-1 P0. v2/v2.1 selector
  coverage is now mapped; physical operation and the phone's controller remain
  unknown. Old source contains both ST and Atmel touch options.
- PMI8998 gauge: direct telemetry is implemented without IMA/DMA or threshold
  writes. Profile integrity/loading, cold-boot initialization, battery ID, learning
  and SOC maintenance are not complete. Do not treat readout as full gauge parity.
- Type-C/PD/OTG, speaker amplifier/voice routing, full haptics effects, NFC, GNSS and sensors
  still need targeted kernel review and matching board bindings. They are NOT
  claimed complete by a module build or by a GPIO description.
- Camera variants/power/CSI, sensor timing/Bayer order, autofocus, OIS and EEPROM
  calibration are still kernel/hardware tasks. Unverified modes must not be
  invented to satisfy an API capability list. IR emitter control remains absent.
- Boot, storage access, regulator sequencing, suspend/wake and thermal behavior
  require controlled hardware tests. Compile success cannot validate these.

The missing information cannot all be deduced from a generic old/new kernel diff:
source-matching 4.9.337 still includes a different Qualcomm BSP and interfaces.
Use pinned, per-subsystem comparisons, not a wholesale replacement of Linux core.

## Actual Android/vendor dependencies identified

- Graphics: legacy KGSL/ION interfaces versus DRM/MSM and DMA-BUF; requires a
  matching graphics allocator/composer/Mesa or another supported userspace stack.
- Camera: stock Qualcomm ioctls/provider versus V4L2/CAMSS sensor/media graphs;
  needs a provider/HAL, metadata, buffer/fence, ISP/3A and calibration integration.
  Camera2 visibility and app support are separate from a kernel sensor driver.
- Audio/voice: old Qualcomm routing/mixer/vendor services do not automatically
  match modern ASoC and DSP/voice paths. Modem data and voice are different tasks.
- Wi-Fi: ath10k plus correct board/calibration data and Android Wi-Fi HAL handling;
  the old qca_cld3 /dev/wlan control path is not the same interface.
- Firmware: image names, format, access and exact versions must match the
  remoteproc/GPU/BT/video stack. No proprietary blobs or identifiers from the owner's device
  are committed. The initial public reference-DT import contained its author's
  example MAC; the adaptation removed it from the active tree without rewriting
  the preserved import history. Existing firmware is not magically updated by a new kernel.
- Boot/init: early module packaging/loading, boot-header/DTB selection, fstab,
  encryption and SELinux/VINTF need device-tree and Android build integration.

Vendor changes have not been started. The independent kernel tasks above remain
open rather than being hidden behind compatibility stubs or disabled checks.

## Before selecting a physical-board test image

`tools/dipper/read-hardware.sh` is an optional, explicitly invoked read-only ADB
inventory. It is not run by any build/verification script and has not been run
against the owner's phone. It does not request root, reboot, write partitions or
extract calibration/serial/IMEI data. It can help resolve the board/controller
identity missing from internet sources; it is not itself a hardware driver test.

## Latest end-to-end check

latest-checked-build.json identifies the checked source revision, configuration,
Image/DTB/plugin hashes and 148 module paths. At source revision
`e4a67475027dc832958db6258321c153945a3a2f` the final verifier completed with
zero applicable schema diagnostics for all 14 final trees and the helper base,
242 source/compiled-DT tests and one million ASan/UBSan decoder cases. It also
rechecked all 97 explicit fragment requests after schema generation. Haptics register encoders also passed ASan/UBSan
coverage of all supported periods and all trim bytes, voltage ceilings and
debounce cases. FG conversion helpers passed all raw-word, revision-byte-order,
SOC and temperature cases under ASan/UBSan. The unchanged legacy manifest was reproduced in the earlier
variant audit; that result is historical, not a new hardware test.
No Android/vendor files were changed, and no phone command has been executed.

The work is published on topic branches. It does NOT mean that every independent
kernel task is finished: per-revision runtime checks, fuel gauge, Type-C, audio, GNSS, NFC,
sensors and remaining camera/lens work are still tracked above. Some require
physical identification, calibration/protocol data or hardware validation before
we can make defensible changes, in addition to future Android/vendor integration.

# Integration status — 2026-11-08

This is a development kernel, not a stable or flashable crDroid release.
No phone was accessed, rebooted, flashed or otherwise changed.

## Completed offline work

- Hardened EA8074 panel: LP command parity with the old kernel, complete vendor
  poweroff sequence, safe power/backlight serialization and reference lifetime.
- MP v2.1 DT: checked reservations, RPMh, storage, display, USB peripheral,
  Wi-Fi/Bluetooth, charger and video-codec descriptions. No shared calibration
  MAC or guessed live framebuffer/ramoops is copied from another phone.
- IMX363 candidate: constrained RAW mode, cleaned control/PM handling and binding.
  No enabled camera node or claim of AF/OIS/HDR/video-mode parity.
- New runtime-only FTM5 driver, not the firmware-updating downstream code.
  Compile-selected, with a deliberately disabled board node pending identification.
- Explicit fragment requests are checked after Kconfig; dependencies cannot
  silently drop a requested driver. Rust, CFI, SELinux and BTF remain enabled.
- Combined build produced Image, MP v2.1 DTB and 145 modules on a clean Git tree.
  Previous result records are historical; they identify the exact source revision.

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

- Board/controller identification: old source contains 15 board descriptions and
  both ST and Atmel touch options. This phone's actual revision is still unknown.
- PMI8998 gauge: the available downstream candidate writes IMA/DMA control and
  differs across PMIC revisions. A direct unchecked import is not a validated
  battery driver. Verify revision/byte ordering/ready state and retain calibration.
- Type-C/PD/OTG, speaker amplifier/voice routing, haptics, NFC, GNSS and sensors
  still need targeted kernel review and matching board bindings. They are NOT
  claimed complete by the 145-module build or by a GPIO description.
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
  remoteproc/GPU/BT/video stack. No proprietary blobs or per-device identifiers
  are committed. Existing firmware is not magically updated by a new kernel.
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

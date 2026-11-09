# MP v2.1 board integration (compile-tested only)

The generic dipper DT from Max Furman's port is retained in Git history with its
authorship. The initial adapted ACK target was explicitly named
`sdm845-xiaomi-dipper-mp-v2.1.dtb`. Its hardware description now lives in a common
include used by the audited v2/v2.1 wrappers; see board-variants.md for precise
coverage and the new overlay-base contract. qcom board ID 55 and SDM845 revision
0x20001 still identify MP v2.1. The active phone's revision is not yet known.

## Described using existing ACK drivers

RPMh power supplies, CPU/interconnect/thermal description inherited from SDM845,
UFS and its PHY, EA8074/DRM display, Adreno 630, USB2 peripheral mode and PHY,
volume keys/PMIC power key, WCN3990 Wi-Fi/Bluetooth, Venus video codec, SMB2
charger. A DT node's presence is not a hardware test or Android HAL integration.

Wi-Fi board/calibration data and all named firmware files must match this phone;
no device-specific MAC, calibration or proprietary blobs are committed.

## Deliberate changes relative to the reference

- Retain the final downstream secure-memory exclusion through 0x8a900000,
  including the larger override in dipper-p0.dtsi (not just generic SDM845 TZ).
- Reserve the full two-megabyte XBL area from the old kernel, not just the last MB.
- Preserve the old ADSP/WLAN/modem/video/CDSP/MBA/SLPI/SPSS addresses and sizes.
- Allocate a guarded RMTFS pool through reserved-memory instead of hardcoding the
  reference author's runtime address. RMTFS userspace/protocol still needs review.
- Reserve the old splash and ramdump framebuffer areas without creating a live
  simple-framebuffer. Restore ramoops at 0xb0000000 with the exact downstream
  sizes; the resolved old board source, not another phone's runtime DT, proves
  these constants. The earlier description incorrectly called ramoops unverified.
- No hard-coded MAC, foreign USB VID/PID, or forced always-on camera rails.
- No unsupported q6voice, FTM5, TAS2557 or fuel-gauge nodes without their drivers.
- USB is peripheral-only; do not advertise PD, OTG or source VBUS before the
  PMI8998 controller implementation and role/power sequencing are reviewed.
- Panel L14 is consumer-managed. The existing L2 boot reference-clock supply
  remains always-on pending a proper consumer; old unused UFS DT properties do
  not substitute for a modern driver implementation.
- ADSP/CDSP/modem/SLPI remain disabled pending firmware and userspace integration.
  Their reserved areas are retained. Camera/audio/touch are separate work items.

## Validation

DTC compilation passed. Full dt-validate using the generated ACK schema database
produced an empty diagnostic JSON array after fixing regulator-node names, UART
pinctrl child names and obsolete UFS properties. W=1 additionally emits two
inherited SoC warnings (DSI1 address cells and duplicate QFPROM fuse offset);
these are not suppressed in source or claimed as fixed here.

Run the compiled DT tests with the dtschema venv Python, setting DIPPER_DTB to
the compiled file, followed by `-m unittest discover -s tools/dipper/tests -v`.
The reserved-memory regression test checks the union of new regions against
**every final fixed carveout** in the preprocessed old tree, including no-map.
Before the correction it failed for secure memory, splash, ramdump and ramoops.
These tests verify region overlap, identity, guarded RMTFS allocation and absence
of known unsafe carryovers, not
physical operation, modem compatibility, charging safety or suspend current.

The charger uses the reference board's 4.4V design maximum. Hardware battery
variant, board revision and recovery must be checked before any deployment.

## Resident-touch candidate

A new `st,stmfts-ftm5` node describes the legacy GPIO31 IRQ, GPIO32 reset, I2C
address 0x49, GPIO23 I/O regulator and L28 analog regulator. It remains disabled:
the owner's actual board/controller has not been identified, and resident
firmware behavior needs a controlled crDroid test before enabling it. The new
runtime-only driver is compile-selected by core.config, not the old downstream
firmware-updating driver. No always-on vote is introduced for either touch rail.

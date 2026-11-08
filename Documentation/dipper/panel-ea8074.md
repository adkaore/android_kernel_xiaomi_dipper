# EA8074 panel development

Branch: work/panel-ea8074. Compile-tested candidate; not enabled in a board DTS
or integrated into the port branch yet. No device has been flashed.

## Provenance

Imported binding and driver from taygoth/dipper-mainline-port, pinned in
sources.json. Original author metadata and GPL notices preserved by git am.
The driver patch's Makefile hunk was excluded because its context belongs to
another kernel version; the corresponding ACK Makefile entry is a separate commit.

## Legacy cross-check

Reference: legacy/15.0, arch/arm64/boot/dts/qcom/
dsi-panel-ss-notch-fhd-ea8074-cmd.dtsi.
The candidate matches 1080x2248, 60 Hz, 68x142 mm, four RGB888 lanes,
horizontal porches 56/56 and pulse 18, vertical porches 26/24 and pulse 12.
Reset sequence must use an active-low GPIO in the future board DTS to translate
the driver's logical 0/1/0 to the legacy physical 1/0/1 sequence.
These comparisons do not establish electrical correctness for every board revision.

## Changes beyond import

- Replace removed mipi_dsi_dcs_write_seq with the modern multi-context API;
  check accumulated errors before proceeding with the next initialization stage.
- Display-on now belongs to enable, display-off to disable; sleep belongs to
  unprepare. This supports disable/enable without power cycling.
- Initial DCS brightness is zero, with the DRM backlight helper applying the
  requested brightness on enable, avoiding initial unconditional full brightness.

## Checks

- Google Clang r596125: W=1 object compilation passed.
- Complete .ko build, MODPOST and BTF generation passed against the GKI baseline.
- checkpatch --strict: zero errors/warnings/checks.
- dt-doc-validate passed; binding example compilation/schema validation passed.
- No display, brightness or suspend behavior has been tested on hardware.

## Required before integration

Review regulator load requests, shutdown/unbind behavior and all failure paths;
validate the board GPIO/supply wiring and matching display host configuration.
The imported driver does not establish AOD, HDR, HBM, DC dimming or calibration
parity with the old Xiaomi stack. Those features require separate review.
Do not silently treat a working compiler as proof of those features.

Build outputs remain outside Git. The baseline output directory was reused for
a focused module compile; rebuild baseline with its script before comparing it.

## 2026-11-08 hardening

The old DT explicitly requests LP mode for both on and off commands. The
candidate incorrectly changed the off path to HS; all DCS paths now retain LP.
The missing seven manufacturer power-off commands were restored byte-for-byte
from the pinned old DT, including the delay before sleep. Initialization bytes
are checked against the same source by tools/dipper/tests/test_panel.py.

The panel now uses reference-counted devm_drm_panel_alloc, initially asserted
reset, regulator bulk init_load_uA and mutex-protected power/backlight state.
Brightness writes while unpowered are cached by the backlight core without DSI
transfers. Failed regulator shutdown is no longer reported as success; powered
state is retained for retry. Remove/shutdown request panel power-off before DSI
detachment. No regulator load is advertised as a battery optimization: the
regulator core already excludes disabled consumers from aggregate current load.

These are source-regression and compiler checks, not a mock hardware test.

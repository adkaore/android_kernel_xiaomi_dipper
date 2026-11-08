# Camera kernel work — experimental, not wired to a live board

IMX363 driver provenance is recorded in camera-sources.json; original patch
and authorship remain in Git history. This is a candidate V4L2 RAW sensor driver,
not an Android camera HAL, an ISP pipeline or an image-quality implementation.
The sensor node is deliberately absent from the MP v2.1 DT until wiring, power
rails and calibration variants are verified. No camera firmware was flashed.

## Source changes and checks

- Original full-resolution 4032x3024 register writes are retained. Source tests
  compare every active CCI_REG8 entry against the imported patch.
- Only its candidate four-lane/24MHz/636MHz RAW mode is advertised. Experimental
  HD, two-lane and HDR combinations are not advertised as working capabilities.
- Default frame length matches the actual 0x0340/0x0341 bytes (3170 lines).
  No measured FPS is claimed; sensor PLL, pixel-rate and CSI timing need hardware
  verification. A source table or code comment is not a sensor datasheet.
- Correct lane-config indexing, checked clock acquisition/rate, freed endpoint on
  GPIO failure, bounded exposure and balanced repeated stream requests.
- Block ACTIVE format changes and layout flips during streaming.
- Start clock before reset release; retain the referenced port's supply timing.
- Active-low reset semantics match the new binding; copied active-high reset
  nodes from the reference port MUST NOT be used unchanged with this driver.
- Regulator suspend failure is propagated; the bulk rollback keeps clocks
  running and reset released for the still-active runtime-PM state.
- Runtime PM is enabled before exposing the subdevice to avoid a registration
  window with inaccessible controls.
- Remove unverified native-array/crop geometry inherited from another template.

Clang r596125 W=1/-Werror object compile passed; binding/example validation and
nine source regression tests passed. Full module/link testing is a separate
integration build. Source checks are not runtime fault injection or hardware
validation. checkpatch has no errors/warnings but seven formatting/macro checks.

## Still required in kernel (not excused by vendor changes)

Confirm IMX363 rail sequencing, native array/timing/format/Bayer order from
hardware or authoritative data; add board-specific power providers instead of
always-on enable GPIOs. Implement/review lens actuator and OIS independently of
sensor RAW capture. Map all physical camera variants from legacy device trees.
The existing ACK OV7251 driver is a candidate for IR RAW capture, not proof of
working IR video on Mi 8. Neither this import nor a HAL can add unsupported
physical sensor modes. Do not emit undocumented register writes to claim them.

## Android boundary

A Camera2 interface exposing physical cameras and video modes requires an
Android camera provider/HAL plus buffer/fence handling, metadata, stream
configuration and ISP/3A/calibration support. The old proprietary Qualcomm camera
stack expects its old kernel interfaces; the V4L2 CAMSS stack does not reproduce
those interfaces. Which cameras third-party apps open also depends on the app,
permissions and platform exposure; kernel code cannot guarantee every app uses
all cameras. IR emitter control is not included or enabled by this work.

# Target audit — crDroid 13.1 / Android 17

Checked 2026-11-09. Source commits, inspected files and SHA-256 hashes are in
crdroid13-target-audit.json. Nothing here is a completed Android build, VINTF
check or hardware validation. crDroid 11.17 remains a **hardware/behavior
comparison source**, not the target userspace specification.

The exact requested release is **13.1**. The inspected platform vendor commit
15b1b035ba60e0a92138adc905d8a187cacde654 sets CR_VERSION := 13.1 in
config/version.mk and Android product version 17.0. The manifest/project branch
remains 17.0, not a guessed 13.1 Git branch. This is the same platform vendor
revision observed in the initial audit, now checked locally. A release version
does not pin every moving manifest project or supply a dipper product definition.

## Available target sources

The official crDroid manifest has a public 17.0 branch, pinned here at
823087de2860e9fb1f1cd7ce8493693ace73e7ad. It uses Android 17.0.0_r1 AOSP projects,
crDroid 17.0 overrides, and LineageOS lineage-24.0 as its default GitHub revision.
The relevant projects are:

- hardware/interfaces: crdroidandroid/android_hardware_interfaces, 17.0,
  a10a565d478b24e483d495cb2736f77f72756c56.
- hardware/nxp/nfc: LineageOS/android_hardware_nxp_nfc, lineage-24.0,
  0b8bf91ca3a41a207ccb7429a553dac771433ca2.
- hardware/nxp/secure_element: LineageOS/android_hardware_nxp_secure_element,
  lineage-24.0 (present in the manifest; not audited end-to-end here).
- vendor/lineage is supplied by crdroidandroid/android_vendor_crdroid on 17.0;
  this platform vendor tree is not the device-specific proprietary vendor tree.

Public 17.0 ref queries returned no matches for crDroid's dipper device tree,
sdm845-common tree, proprietary_vendor_xiaomi_dipper or its sdm845-common vendor
repository. This is a bounded observation at those public endpoints, not proof
that private/unofficial work does not exist. Dipper 15.0 or common 16.0 must not
be silently substituted for a validated Android 17 product tree.

## NFC: what is established, and what is not

The manifest-selected NXP source offers android.hardware.nfc-service.nxp (AIDL
V1) and android.hardware.nfc2-service.nxp (AIDL V2). Its inspected I2C transport
opens a configured character device and uses read/write/ioctl. The key
NXP_NFC_DEV_NODE is configurable; the inspected snxxx fallback is /dev/nxp-nci.
A device name such as /dev/nq-nci is therefore not imposed by Android 17 itself.
Conversely, an Android HAL speaking the NCI wire protocol is not automatically
a client of the Linux AF_NFC/NCI networking stack: those are different layers.

The modern transport includes ESE power, reset-read-pending and GPIO-status
ioctls. The old Mi 8 nq-nci implementation has different behavior for overlapping
operation numbers (including old eSE 0=on/1=off versus the inspected modern
header's 1=on/0=off), and lacks real implementations for some commands. A shared
pathname or matching power-on ioctl alone is **not** an ABI compatibility proof.
The PN553-related code in this source is evidence of available code, not proof
that a particular dipper product selects the right service, options and firmware.

The platform FCM 202604 example accepts NFC AIDL 1-2, health AIDL 3-5 and vibrator
AIDL 1-4. These ranges do not establish the correct FCM/shipping-API level for
this upgraded Mi 8, nor require every old device to adopt a new-launch matrix.
The actual device manifest and product/vendor selection must be inspected and
checked together. No VINTF enforcement was disabled to avoid this requirement.

**Decision: deferred.** Neither preserving nq-nci nor adopting Linux NCI is an
integrated requirement yet. work/nfc-nci-clock is an isolated generic-driver
reference topic; its earlier architectural wording based on the 15.0 device
configuration is superseded by this target audit. No nq-nci driver port was
started, no NFC module was added to the phone's core fragment, and no NFC node
was added/enabled there. The kernel target remains crDroid 13.1, not crDroid 11.

## Evidence needed before choosing the NFC implementation

1. Android 17 dipper product/device tree and dependencies, or an explicit,
   reviewed new product definition for this port.
2. Selected NFC service/version and transport variant, including build flags.
3. Target vendor configuration, firmware contract and secure-element ownership.
4. A device manifest/compatibility matrix, SELinux and ueventd policy matching
   the actual driver ABI, including scalar/pointer and compat ioctl semantics.
5. A complete operation/argument audit, build/VINTF checks, then controlled tests.

## Independent kernel work remains valid but is not target certification

Reserved-memory coverage, panel command/power handling, input decoding and
read-only PMIC telemetry can be implemented and checked independently. Their
native kernel APIs do not certify a target Android health/vibrator/camera HAL.
For example the FG frontend reports real direct telemetry but does not program
profiles or expose learned counters; the target health service may require
additional properties and policy. Those gaps remain explicit. Build success and
legacy behavioral correspondence must never be relabeled crDroid 13.1 compatibility.

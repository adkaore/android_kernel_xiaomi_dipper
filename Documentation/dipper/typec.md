# PMI8998 Type-C / PD / VBUS candidate

Target: crDroid 13.1 / Android 17. This is independent kernel implementation,
not a certified Android USB HAL/product integration. No phone was accessed.
The source pins and reference-patch hashes are in typec-sources.json.

## Implemented, not enabled

- PMI8998-specific CC backend derived from Max Furman's GPLv2 work, preserving
  both original import commits/authorship. It uses USBIN 0x1300 and one rollup
  IRQ, not PM8150B's different CC register block. PMIC identity, base and USID
  parity are checked before setup. The PD PHY registers/IRQ order were compared
  with the pinned downstream qpnp-pdphy.c, not inferred from a newer PMIC name.
- CC OPEN really sets DISABLE, rather than zero (which downstream defines as
  dual-role search). Setup leaves CC OPEN before synchronous TCPM registration.
  Unknown, conflicting or not-yet-debounced attached status never becomes a
  guessed Rp/Rd. Two-Rd debug and two-Ra audio combinations report both lines.
  Bounded status rechecks reduce mixed role/orientation snapshots, but are not
  a hardware-atomic snapshot. Rp/current-source encodings from the reference
  candidate still require hardware validation. Advertising above the boost
  regulator's configured current limit is rejected.
- One mutex serializes each new CC backend's sleeping register accesses and
  VBUS accounting. IRQ is requested NO_AUTOEN, attached to a live TCPM object
  before enabling, and drained outside the mutex on teardown. Teardown blocks
  new enabling callbacks before draining the consumer. Initial cable state and
  debounce completion queue fresh state reads rather than relying on an XOR
  snapshot which can miss an event. VCONN enable rejects absent/overcurrent
  state, and overcurrent interrupts attempt to switch it off.
- The shared PD PHY uses a mutex, propagates reset I/O failures, prepares power
  before TCPM's synchronous init, drains reset work before invalidating its
  TCPM pointer, and gates callbacks after stop. Invalid RX lengths used to
  reach TCPM with uninitialized data; frames are now zero-initialized, bounds,
  SOP type and header/object-count length are checked, and RX ownership is
  returned even on dropped frames. Hard reset and cable reset now use different
  signal encodings, matching downstream; signal TX completion is reported.
  Only SOP messages and hard/cable-reset signals are accepted for transmission.
  No SOP-prime/cable discovery, FRS, alternate-mode or PPS parity is claimed.
- A distinct PMI8998 VBUS provider has the real 250 mA..1.5 A selector table.
  The reference patch's PM8150B fallback would incorrectly use 500 mA..3 A.
  Descriptors are per-instance instead of mutable global offsets. Software
  ownership is established before publication; an unaccounted bootloader boost
  enable is cleared. Enable performs downstream's secure HALT_1_IN_8 sequence
  and bounds soft-start/state polling with rollback. No unsupported PM660 OTG
  workaround or automatic overcurrent retry is copied into this PMI8998 path.
- When an enabled PMI8998 Type-C sibling is described, SMB2 initialization
  leaves CC/VCONN registers to that sibling regardless of probe order. Type-C
  setup then owns the factory/micro-USB/VCONN-OC configuration. A disabled
  sibling does not alter the currently enabled charger's legacy init path.

## Board evidence and deliberate gating

All twelve standalone and two composed candidates include **disabled** Type-C
and VBUS descriptions. PD PHY supply is PM8998 L24 fixed at 3.088 V, matching
resolved dipper source, not a OnePlus rail. Dipper overrides the generic external
boost phandle with smb2_vbus; no foreign GPIO regulator is copied. Its old PD
voltage-limit override is 9 V, even though the inherited generic array includes
12 V. Neither array nor a foreign identity is copied into the new board.

The candidate boost constraint is a conservative 500 mA for future controlled
bring-up, NOT a verified shipping source contract. There is no connector child,
PDO set, PD identity, Type-C role graph or change from existing USB peripheral
mode. Simply changing status to okay is insufficient: the driver requires a
connector, and the missing charging/role work below must be completed first.
Both drivers are compile-selected; module presence does not enable either node.

## Still kernel work, not all a vendor blocker

1. Complete charger/TCPM policy coordination: negotiated sink input-current and
   voltage limits, input suspension during boost, detach/role-swap ordering and
   OTG fault handling. Register-ownership separation alone does not implement
   these. The existing charger's conservative fast-charge limit remains intact.
2. Validate Rp encodings, disable/restart timing, CC2/PBS revision workarounds,
   VCONN limits and boost timing/protection against the exact PMIC revision.
   Offline decoder tests cannot prove analogue behavior or waveform timing.
3. Provide a real VBUS measurement/vSafe0V strategy. TCPM consumes get_vbus as a
   boolean even on negative I/O errors; this candidate logs/returns an error
   rather than inventing absence, conservatively avoiding a false zero-voltage
   claim. A software boost vote or settling delay is NOT a voltage measurement.
4. Define dipper's connector and DWC3 role-switch graph only after these safety
   and power paths are coherent. The old kernel has additional source/sink,
   fault and charging policy that a wire-protocol register comparison cannot
   replace. Then validate data roles, power roles, suspend/wake and removals.
5. Check the actual crDroid 13.1 USB HAL, product VINTF and vendor policy. Target
   dipper device/vendor selection is still unconfirmed; old crDroid 11.17
   behavior and a built TCPM module are not evidence of target compatibility.

## Offline verification

The verifier includes both new bindings, all candidate DTBs, source contracts
and the real pure CC decoder / PD length validator under ASan+UBSan. The host
suite enumerates 33,554,432 byte/header/length combinations. This does not
simulate kernel IRQ, concurrency, regulator I/O failures, PD timing or hardware.
Exact clean-build provenance/results are in latest-checked-build.json; older
records continue to describe their own earlier source revisions.

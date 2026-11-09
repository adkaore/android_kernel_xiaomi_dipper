# Remaining source work ledger

Target: crDroid 13.1 / Android 17. No phone access is authorized by this ledger.
This is a work breakdown, not a claim that its first completed subset exhausts
all work possible without a phone. New findings must be added, not hidden as
generic vendor/hardware blockers. Finished source work still needs real tests.

| ID | Work package | Source deliverable / acceptance | State |
|---|---|---|---|
| P1 | PMI8998 watchdog | Check all three BSPs; ordered start/stop/pet and wake handling; real-callback error tests | Implemented; final clean verification pending; physical wake unvalidated |
| P2 | JEITA/thermal | Resolve public battery/charger policies per board; identify valid ranges and suspicious values before porting | Source deliverable done in battery-profiles.md/json; native policy is still implementation work |
| G1 | Gauge profiles | Inventory exact profile payload/hash, battery-ID ranges, algorithm and persistence dependencies | Source deliverable done; no loaded-profile validation/programming claimed |
| G2 | Gauge runtime | Execute real direct-register callbacks with read errors, readiness loss, mixed SOC copies and both revisions | Pending source work |
| U1 | Type-C revision errata | Map raw revision to CC2/PBS/QC workarounds; implement only reviewed ownership-safe steps | Pending source work |
| C1 | Camera/lens/IR | Map source nodes, rails, addresses, reset and emitter controls across revisions; identify absent sensor programs/calibration | Pending source work |
| A1 | Audio/sensors/GNSS | Map modern kernel providers versus downstream protocols; audit available speaker candidate and firmware dependencies | Pending source work |
| B1 | Boot/module closure | Verify DT compatible/module/firmware requirements and packaging assumptions, without claiming Android boot | Pending source work |
| B2 | Update workflow | Test merge/update isolation and conflicts on disposable Git graphs; preserve the actual working port, not an old branch | Pending source work |
| V1 | Final verification | Clean normal build, schema/host suites, ARM64 KUnit, checked provenance and pushed topic commits | Repeat after changes |

## Not acceptable substitutes for completion

- A file inventory is not a driver port; the inventory must explicitly retain
  unimplemented driver work. A module build is not functional compatibility.
- Legacy crDroid/Xiaomi parameters are hardware evidence, not the crDroid 13.1
  HAL specification. The target dipper product/vendor remains unconfirmed.
- Unknown battery identity, calibration and analogue timing must not be guessed.
  Conversely, an unfinished algorithm or register audit is still source work,
  not automatically a reason to request the owner's phone.
- No NFC character-device ABI is chosen until the actual target NFC HAL/product
  requires it. Existing experimental NFC code stays on its isolated branch.

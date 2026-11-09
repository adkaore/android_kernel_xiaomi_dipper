# ARM64 kernel API regression tests

Target remains crDroid 13.1 / Android 17. These tests do not boot Android or
emulate a Xiaomi PMIC. They run the actual static PMI8998 Type-C handoff,
current-limit and SMB2 register-policy callbacks in an ARM64 Linux kernel
under QEMU's `virt` machine.
On the development x86 WSL host QEMU uses TCG. No phone or physical device
passthrough is involved. This is a separate, minimal test configuration, not
the production GKI fragment or a phone boot image.

## Execute

Install QEMU's aarch64 system emulator and provision the pinned Google compiler
first. The runner does not install packages or require root:

    TOOLCHAINS_DIR=/home/adkaore/android/toolchains tools/dipper/test-pmi8998-kunit.sh /home/adkaore/android/out/pmi8998-kunit-arm64

The former `test-typec-kunit.sh` entry point is retained as a wrapper; it now
executes both suites too.

Output must be external to the source tree. The runner rejects an incomplete,
zero-test, skipped, duplicate, crashed or wrong-suite result. It also rejects
kernel WARN/BUG/panic/lock-order diagnostics. A previous success report is
removed before a new run. The generated `dipper-kunit-report.json` records the
actual Git revision/dirty state, compiler/emulator versions and hashes of the
kernel, config, raw boot log and harness results. The harness's `git_branch`
placeholder is NOT used as source provenance. Run from a clean commit when
publishing results; the source must not change during a run.

## Fourteen Type-C cases, real framework calls

The test file is privately included by the driver under a default-off Kconfig
option; the normal modular GKI driver does not include the tests. No production
callbacks are exported or copied. A real kernel regulator provider/consumer is
registered and devres/KUnit actions release it. The input supply is synthetic;
the callback still goes through the real `power_supply_set_property()` path.

- Source/sink direction and OFF ordering; sink request with boost already OFF.
- Input-suspend failure prevents boost; boost failure leaves input suspended.
- Boost-disable failure prevents sink resume; input-resume errors propagate and
  can be retried without another regulator disable.
- Repeated source requests do not leak a regulator enable reference; a failed
  enable is retryable.
- Invalid simultaneous roles and enabling after the stop gate are rejected.
- Current bounds, non-5-V rejection, multiplication overflow boundary, reset,
  supplier error propagation and the current callback's stop gate.

In this suite the synthetic input setter confirms isolation immediately; it
does not prove the physical input suspended.

## SMB2 register and watchdog cases, real regmap and driver methods

A second default-off suite is privately included in the native SMB2 driver.
It uses a real, uncached regmap with a bounded synthetic register bus. All
accesses must include the expected charger base; unexpected addresses fail the
test. The real suspend poll runs with immediate, delayed, missing and failed
status reads; the test does not replace it with a copied loop or shortened
timeout. No successful command write is mistaken for an ACK in the timeout
case. These are simulated register responses, NOT measurements of a PMIC.

Other cases check command RMW read/write errors, preservation of unrelated
bits, resume without a suspend poll, unmanaged-owner rejection, invalid ONLINE
values, signed/board-bounded current writes and never raising low/zero limits.
Readiness and stop gates, both charging-disable steps on failure, health read
errors and sysfs policy are checked too. The valid/invalid public getter/setter
paths used here run through power_supply; successful input/current programming
is tested through the real internal methods under their required mutex. This
does not exercise successful property-change notification work or all of probe.
Additional watchdog cases execute configuration, start/stop, failed pets,
latched failure and cleanup, with PM/suspend compiled in. The latest checked
report lists every expected case; see watchdog.md for the distinct wake scope.

Both suites keep lockdep, atomic-sleep checking, stack-pattern initialization
and RANDSTRUCT enabled. They are separate tests, not a complete connected
TCPM/SMB2/PD/IRQ simulation, nor all possible concurrency scheduling. Hardware
acknowledgement semantics, electrical/VBUS/PD operation and wake timing remain
unvalidated. Board Type-C and boost remain disabled.

## Generic ACK allocator regression found by booting tests

Before the allocator fix `afcbd803f2fa`, both UML/GCC and ARM64/Google Clang
failed before KUnit could start. In `__alloc_frozen_pages_noprof()` a local page
pointer was passed to the optional Android allocation hook without initializing
it. With vendor hooks off/no provider, the subsequent `if (page)` could return
an uninitialized stack value instead of allocating from the buddy allocator.
ARM64 with stack-pattern initialization returned `0xaaaaaaaaaaaaaaaa` and
faulted in `new_slab`; UML similarly faulted using its `0xfefefefe` pattern.

The one-line fix initializes the optional result to NULL before the hook. It is
a general ACK correctness fix, on its own topic branch, not a Xiaomi hardware
delta or a reason to disable stack hardening. The ARM64 test config explicitly
keeps pattern initialization enabled and vendor hooks disabled so merely
reaching the test executor also exercises the previously failing boot path.
The runtime result, not a source assertion alone, is the regression evidence.

These build scripts are non-hermetic. Pinned versions and hashes are useful
reproduction records, not signed supply-chain or target-HAL certification.

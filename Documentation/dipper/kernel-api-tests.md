# ARM64 kernel API regression tests

Target remains crDroid 13.1 / Android 17. These tests do not boot Android or
emulate a Xiaomi PMIC. They run the actual static PMI8998 Type-C handoff and
current-limit callbacks in an ARM64 Linux kernel under QEMU's `virt` machine.
On the development x86 WSL host QEMU uses TCG. No phone or physical device
passthrough is involved. This is a separate, minimal test configuration, not
the production GKI fragment or a phone boot image.

## Execute

Install QEMU's aarch64 system emulator and provision the pinned Google compiler
first. The runner does not install packages or require root:

    TOOLCHAINS_DIR=/home/adkaore/android/toolchains tools/dipper/test-typec-kunit.sh /home/adkaore/android/out/pmi8998-kunit-arm64

Output must be external to the source tree. The runner rejects an incomplete,
zero-test, skipped, duplicate, crashed or wrong-suite result. It also rejects
kernel WARN/BUG/panic/lock-order diagnostics. A previous success report is
removed before a new run. The generated `dipper-kunit-report.json` records the
actual Git revision/dirty state, compiler/emulator versions and hashes of the
kernel, config, raw boot log and harness results. The harness's `git_branch`
placeholder is NOT used as source provenance. Run from a clean commit when
publishing results; the source must not change during a run.

## Fourteen cases, real framework calls

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

Lockdep, atomic-sleep checking, stack-pattern initialization and RANDSTRUCT stay
enabled. This is not all possible concurrency scheduling, a full TCPM role-swap
simulation, an SMB2 regmap ACK test or an electrical/VBUS/PD validation. In
particular the synthetic input setter confirms isolation immediately; it does
not prove the physical input suspended. Board Type-C and boost remain disabled.

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

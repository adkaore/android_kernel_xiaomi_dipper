# Build baseline — 2026-11-07

## Passed

- Host dependency inventory after package installation.
- ARM64 defconfig generation with Ubuntu Clang 21.1.8.
- Stock DTB compilation for SDM845 Xiaomi polaris and both beryllium panels.
- No changes to any kernel/driver/DTS source were needed for this check.

These are compiler/parser smoke tests, NOT Mi 8 device support, a full kernel,
a flashable boot image or DT schema validation. Do not flash these DTBs on Mi 8.

Reproduce from the repository:

    tools/dipper/build-baseline-dt.sh /home/adkaore/android/out/baseline-dt

The external output directory receives build-report.json, recording the Git
revision, dirty state, compiler version and SHA256 of configuration and DTBs.
Default concurrency is 8; override with JOBS. Outputs are not committed.

## Official ACK build toolchain

The inspected Google tree's bazel/constants.scl requires:

- Clang r596125
- Rust 1.91.1.p3

The installed Ubuntu compiler is adequate for the DT smoke test, but it is not
substituted for the pinned ACK toolchain. Rust/CFI are not disabled to make a
GKI build appear to pass. build.config.gki.aarch64 explicitly says it is obsolete;
the official build uses Kleaf/Bazel.

Google kernel/manifest, branch common-android17-6.18, inspected revision:
46a1a5274ff0b40419952c861a8958bba5f28cfc.
The manifest defaults to main-kernel-2026 for tooling and overrides the common
kernel to android17-6.18. It also includes unrelated virtual devices, Google
boards, bootloader and test dependencies. Before synchronization, derive a
minimal build workspace and pin the necessary tool repositories; do not blindly
sync the complete manifest into the remaining 50 GiB of Windows disk space.

Next milestone: provision the exact ACK tools and establish a full baseline
kernel build, then start individually reviewed device patches.

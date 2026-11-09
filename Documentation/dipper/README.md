# Mi 8 / crDroid kernel development

Status: offline development builds and tests pass; no hardware boot has been tested.
See integration-status.md for current results and remaining compatibility gaps.
The initial-audit findings below are historical, not a statement that no code was ported.
Target: Xiaomi Mi 8 (dipper), Android 17 / crDroid 13, ACK android17-6.18.
No Mobian/postmarketOS userspace target, flashing scripts or firmware redistribution.

## Git layout

- `upstream/android17-6.18`: fetched Google source; never add device commits here.
- `base/android17-6.18`: local clean baseline, tracking Google.
- `dipper/android17-6.18`: integrated port; integration does not imply hardware validation.
- `work/*`: isolated changes and review. Merge completed changes into the port branch.
- `legacy/15.0`: read-only comparison reference for crDroid 11; never merge this unrelated tree.
- `origin`: user's GitHub repository. No force pushes or rewriting published device commits.

Commits in sources.json pin the actual inspected revisions. The legacy branch tip
is NOT proven to be the exact source revision inside the crDroid 11.17 release ZIP.
Shallow history saves disk space; future merges may require targeted deepening.
Publishing a shallow root into an empty GitHub repository may require more history;
do not solve this by silently squashing away upstream provenance.

## Findings

| Area | Evidence | Consequence |
|---|---|---|
| SoC | 6.18 has sdm845.dtsi, clocks, pinctrl, interconnect, Adreno A6xx, ath10k SNOC and CAMSS | Reuse existing drivers, audit board/firmware requirements |
| Board | No dipper DTS in the Google baseline; legacy has MP/P0/P1/P2 and regional variants | Build a documented variant map; do not assume one board variant fits all |
| Boot | crDroid BoardConfigCommon.mk uses boot header 1, Image.gz-dtb, 4096-byte pages | Preserve compatible packaging; do not copy the old command line without review |
| Display/GPU | Legacy Android graphics stack; modern kernel DRM/MSM is a different interface | Decide the matching Mesa/gralloc/composer stack before claiming Android graphics works |
| Memory | Legacy BoardConfig sets TARGET_USES_ION | Audit DMA-BUF heaps and all userspace consumers; do not paper over allocator mismatch |
| Wi-Fi | Legacy selects qca_cld3 and /dev/wlan | ath10k requires matching Android Wi-Fi integration and firmware handling |
| Sound | Legacy audio HAL 6.0 and Qualcomm mixer paths | Modern ASoC routes require corresponding Android audio integration |
| Camera | Legacy Qualcomm camera stack; candidate port only partial RAW IMX363 | CAMSS presence does not replace camera HAL, ISP processing, AF or OIS |
| Touch | Legacy dipper.config selects ST FTS V521 | Candidate patches need Mobian's missing downstream FTM5 driver; stmfts.c is not assumed equivalent |
| Power | Suspend untested in candidate port README | No battery-life claims; retain thermal protection and measure suspend/wake later |

## Candidate port review

Reference: taygoth/dipper-mainline-port, pinned in sources.json.
It targets Mobian 6.12.91 plus downstream patches, not vanilla ACK.
Checked with git apply --check against the clean Google tree (nothing applied):
- EA8074 binding patch: applies textually.
- EA8074 panel driver patch: Makefile conflict.
- IMX363 patch: required drivers/media/i2c/imx363.c is absent.
- TAS2557 patch: applies textually; binding/firmware and code review still needed.
- Dipper DTS patch: applies textually but references missing companion nodes/drivers.
Its own kernel/README.md identifies missing bindings and downstream dependencies.
This was the initial apply-only review. Since then panel, constrained IMX363,
FTM5 runtime input and shared board candidates have been implemented and tested
offline on topic branches. Their current status is in integration-status.md; a
clean application or compilation is still not a hardware test.

## Integration rules

Use existing Linux subsystems; keep device changes narrow and reviewable.
Do not disable SELinux, thermal safety, encryption or compatibility checks merely
to hide failures. Do not claim stock proprietary HALs work with new driver ABIs.
Preserve authorship and licenses when importing reviewed third-party patches.
Full device validation will be with crDroid, not an alternative distro.
No phone operations until the owner explicitly agrees to a specific test.

## Project gates (not all completed)

1. Done: install/pin the ACK host toolchains.
2. Done offline: establish a pinned unmodified baseline development build.
3. Source map done for listed v2/v2.1 targets; hardware/controller identification remains open.
4. In progress: port bindings, shared board description and individual drivers on topic branches.
5. Integrate Android-side components in separate device/vendor repositories.
6. Build crDroid boot/system artifacts, then plan recovery and hardware tests.

The WSL virtual disk reports a 1 TB ceiling, NOT available physical storage.
At initial inspection the Windows host had about 52 GiB free; check both filesystems.

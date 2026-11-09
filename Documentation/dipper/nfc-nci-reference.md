# Generic Linux NCI reference topic — not the crDroid NFC target

Local crDroid 15.0 device sources explicitly configure NXP_NFC_DEV_NODE as
/dev/nq-nci and install android.hardware.nfc-service.nxp. Replacing this ABI
with the Linux NCI networking stack would unnecessarily require an NFC HAL
rewrite. Therefore this generic-NCI topic is separate and does not select a
new driver or enable any board node in the core fragment. The Android target
must preserve the real nq-nci transport ABI, not create a dummy device file.

During reference review, optional reference-clock handling and existing generic
NCI lifecycle/firmware defects were addressed: transport start before device
publication, IRQ stop before free, no unregister under its close callback's
mutex, mode state only after successful activation, gated level IRQs, balanced
clock/wake votes, asynchronous firmware response timeout instead of waiting
under the IRQ receive mutex, and truncated-image/status validation. The firmware
protocol remains an explicit existing API; no image was fetched/programmed and
no phone operation was performed. These changes do not provide nq-nci ioctls,
secure-element arbitration or a tested Android NFC implementation.

The modified NCI objects compile with W=1 and -Werror. Standalone ASan/UBSan
parser tests cover all header words and available lengths 0..1026. This is not
an executed kernel concurrency test or validation of a firmware transfer. The
latest integrated build report still describes the earlier FG source revision,
not this unselected reference topic. Do not merge its configuration by assuming
that every NXP NCI driver implements the crDroid userspace contract.

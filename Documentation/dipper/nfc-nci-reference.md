# Generic Linux NCI reference topic — not the crDroid NFC target

The target is crDroid 13 / Android 17. The old 15.0 dipper source uses nq-nci,
but that alone does not establish the target NFC HAL or kernel transport.
The target manifest and available NXP HAL were inspected and pinned separately;
see crdroid13-target-audit.md/json. The exact dipper Android 17 product/vendor
selection remains unconfirmed. Neither nq-nci nor Linux NCI is selected for the
phone based on age, an old pathname, or the existence of this reference code.
This topic remains isolated and adds no NFC option/node to the core target.

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

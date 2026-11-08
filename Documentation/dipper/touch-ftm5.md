# Resident-firmware FTM5 development driver

A new small I2C/input driver, not an import of the 35,000-line downstream stack.
Protocol IDs, FIFO layout, coordinate extraction and chip bytes are based on
STMicroelectronics' GPLv2 fts_521 source at the pinned legacy revision. The
copyright/license provenance is retained. No firmware binary is redistributed.

## Intentional safety boundary

Only the existing controller firmware is used. The driver has no flash writer,
firmware loader, manufacturing calibration commands, arbitrary register sysfs
or debug proc interface. It checks controller-ready and chip bytes 0x36/0x39,
then starts only the multi-touch scan mask 0x01. A boot CRC/error or absent
resident firmware fails probe rather than replacing firmware or calibration.

The upstream stmfts driver cannot be reused unchanged: its reset/sense commands
and contact byte layout differ from this FTM5 protocol. The Mobian downstream
FTM5 candidate includes an embedded firmware array, automatic updates and tuning;
those are not copied here. This is a deliberate runtime-only implementation.

## Runtime behavior

Standard ten-slot type-B finger/glove/palm input, generic touchscreen coordinate
properties, and no transmission of unsupported stylus/hover or wake gestures.
The input mutex serializes power transitions with open/close and system sleep.
IRQF_NO_AUTOEN prevents access before startup. Atomic IRQ state plus synchronize_irq
balances close/suspend with an in-flight fault; IRQ faults stop a level interrupt
rather than spinning. Recovery is a later reopen/resume, not uncontrolled resets.
All contacts are released on fault/shutdown. Unbind stops input before IRQ teardown.

Supplies are consumer-managed. No PM QoS pinning, high-priority polling loop,
always-on rail or persistent firmware/calibration write is introduced.
Supply-disable errors are logged; a failed system suspend attempts to restore
input because that device might not receive a later resume callback.

## Tests and limits

The actual shared C decoder passed targeted slot/type/coordinate cases plus
one million deterministic fuzz packets with ASan and UBSan. It does not read
outside its eight-byte event, modify input packets or emit invalid slot indices.
`tools/dipper/test-ftm5.sh /absolute/external/output` reproduces that test.
Source tests cross-check protocol IDs against the old tree and guard the
absence of flash/debug interfaces. This is NOT runtime power/IRQ fault injection.

The .ko is compile/link tested with Google Clang r596125, W=1/-Werror; binding
and example checks are recorded separately. No real I2C transfer, touch latency,
resume, calibration compatibility or battery measurement has been performed.
Do not call this a validated replacement until crDroid hardware tests pass.
The MP v2.1 DT candidate remains disabled until the actual touch/board variant
is confirmed. Other Mi 8 revisions may have an Atmel controller instead.

# FH8626V100 native HAL migration

This file records the current migration boundary for FH8626V100 support in Divinus.

## Target shape

The implemented architecture is:

`GC1054/MIPI -> ISP -> VPU/VPSS -> PAE/VENC/JPEG -> Divinus hal streams -> RTSP/fMP4/HTTP`

There is no external FH86 encoded-stream sidecar in the target design. The former `source: fh86` socket owner and its vendor sensor-loader dependency were removed after the native path became authoritative.

## Repository ownership

Divinus owns chip-generic FH8626 media behavior:

- platform/provider identification;
- GC1054/MIPI source implementation;
- ISP initialization and runtime image/AE controls;
- VPU/VPSS/PAE/VENC/JPEG ownership;
- stream dequeue/release and timestamps;
- GraphV2 OSD;
- RTX microphone transport;
- runtime reconfiguration, teardown and telemetry.

The current ANJIA camera integration also supplies recovered board defaults needed by Divinus night control: GPIO18/GPIO60 IR-cut coils, GPIO25 IR LED and stock transition timing. Lens/PTZ/servo behavior, GPIO5 cold bootstrap and speaker-amplifier board policy remain outside the media HAL.

## Migration result

The software migration is no longer blocked by vendor GC1054/MIPI objects, an external ISP profile file, missing same-boot teardown, missing JPEG/MJPEG, missing OSD, or missing runtime H.264 reconfiguration. Those paths are now source-owned.

The exact GC1054 day profile is embedded. H.264 supports the recovered RC modes including fixed-QP and structural runtime changes. GraphV2 OSD and stock-equivalent DAY/NIGHT grayscale are integrated. The anti-flicker public 50/60 Hz mapping is recovered and wired into the same AE context used by the stock algorithm.

## Deliberately unresolved

Two categories remain outside software closure:

1. `audio.gain` dB-to-RTX-raw conversion: the raw hardware control is recovered but no transfer function from the generic Divinus dB value is evidenced.
2. hardware acceptance: camera execution is intentionally deferred and is not inferred from reverse/source completion.

H.265 and temperature are explicit unsupported features rather than unresolved migration work.

## Provider blockers

The native kernel provider currently keeps only target-evidence blockers: integrated audio hardware acceptance and full candidate hardware acceptance. Historical blockers for vendor sensor objects, profile data, runtime reconfigure and same-boot teardown are obsolete and must not be reintroduced.

## Build gate

Before physical-camera testing, the exact `work/fh8626v100` candidate should compile with the OpenIPC ARM/musl toolchain used for FH8626V100. That build is the final software gate; hardware behavior is a separate later phase.

Do not restore the removed sidecar architecture to bypass a native bug. Fix ownership in Divinus or the correctly owning lower layer and keep unrelated camera-specific policy outside the HAL.

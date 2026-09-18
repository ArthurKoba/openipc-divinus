# FH8626V100 native HAL status

This document tracks the current Divinus-native FH8626V100 software implementation and its evidence boundary. `proven` capability state means the contract is recovered and implemented in source. It does not mean the current candidate has passed camera hardware acceptance.

## Architecture

FH8626V100 is a native Divinus HAL platform (`HAL_PLATFORM_FH8626`). Divinus owns the GC1054/MIPI, ISP, VPU/VPSS, PAE/VENC, JPEG/MJPEG, OSD and RTX capture paths directly. The old external `source: fh86` socket/media owner and vendor `libmipi.so` / `libgc1054_mipi.so` runtime dependency are not part of the active implementation.

The OpenIPC target is ARM1176JZF-S / ARMv6KZ soft-EABI on musl. The kernel provider is selected for that target and runtime selection still requires the FH8626V100 identity and required device nodes.

## Implemented source contracts

The current source contains recovered implementations for:

- source-owned GC1054/MIPI initialization, callbacks, 720p25 and 720p30 sensor modes;
- the exact embedded 0xA58 GC1054 day ISP parameter payload;
- ISP context/MMIO initialization, AE/AWB/statistics control and profile LUT publication;
- Bayer-coherent runtime mirror/flip;
- recovered 50/60 Hz anti-flicker: the public Divinus value selects OFF/50/60 and updates the stock AE enable bit plus the numeric frequency field consumed as `500000/frequency`;
- stock-equivalent night grayscale through the shared saturation context, with reversible DAY restore;
- ANJIA AJL33PQ0866 IR-cut/IR LED defaults: GPIO18 DAY coil, GPIO60 NIGHT coil, GPIO25 IR LED, 190 ms bistable pulse and the recovered 100 ms NIGHT ISP-to-IR settle delay;
- VPU/VPSS system/channel ownership, memory setup, frame control and scaling up to the proved 1920x1080 output boundary;
- H.264 Baseline/Main, configurable GOP, CBR/VBR/AVBR/CVBR/fixed-QP, force-IDR and realtime VBR/AVBR bitrate update;
- transactional structural `/api/mp4` reconfiguration with stop/rebuild/start, client epoch isolation, persistent night-state restoration and rollback;
- `MEDIA_STREAM_6` dequeue, ring-wrap handling and exactly-once stream release;
- JPEG snapshots and continuous MJPEG, including quality mapping, QP/VBR/CBR policies and transactional runtime reconfiguration;
- native GraphV2 OSD through `0xC448696D/0xC448696E`, with two global slots and four per-channel slots; Divinus uses main-channel IDs 0..3;
- owner-scoped OSD VMM allocation, transactional bitmap replacement and persistence/retry across structural media-owner restarts;
- native RTX microphone capture through `/dev/rtxbus`, including the recovered retail DSP initialization payload, NR, AI enable/disable and raw AI-volume control;
- balanced same-boot teardown of PAE/VPU/VMM/media ownership;
- monotonic video timestamps used by RTSP/fMP4 and platform/media telemetry.

H.265 and SoC temperature remain explicitly unsupported.

## Remaining semantic limitation

The only intentionally incomplete user-facing mapping in the implemented audio path is `audio.gain`. Divinus exposes this setting in dB, while the recovered FH RTX command accepts a raw hardware AI-volume value. The stock capture path uses raw value 31. No dB-to-raw transfer function has been proved, so FH8626 accepts `gain=0` and does not fabricate a conversion.

This is separate from the RTX capture transport itself, which is implemented in source.

## OSD

FH8626 OSD uses the native GraphV2 kernel request rather than a proprietary ADV_OSD runtime dependency.

- SET: `0xC448696D`
- GET: `0xC448696E`
- wire size: `0x448` bytes
- wire layout: selector + 273-word public GraphV2 record
- main video selector: 1
- per-channel hardware slots exposed by Divinus: 4

Each visible slot owns a dedicated VMM owner. An update fills a new allocation and commits the GraphV2 request before the previous owner is released. Transient owner/resource failures keep the overlay pending rather than silently discarding the update.

## Runtime ownership

FH native calls are serialized around the process-wide kernel context so OSD/JPEG/image controls cannot use a freed media owner while a structural restart is in progress.

A structural video restart disconnects decoder-stateful clients, tears down the current native owner, rebuilds it from the new configuration, restores persistent grayscale state, invalidates persistent overlays for re-upload, and rolls back to the previous configuration if the new owner cannot be made coherent.

The night worker also has an explicit stop lifecycle; `/api/night` no longer joins a worker loop that ignores its stop flag.

## Capability and blocker semantics

`/api/status` separates recovered/source capability state from production readiness. Current source-level capabilities include the native sensor backend, direct ISP bring-up, same-boot teardown, H.264 RC/IDR, 1080p VPSS scaling, JPEG/MJPEG, runtime audio/video reconfiguration, GraphV2 OSD, grayscale, anti-flicker and ANJIA night wiring as `proven`.

The kernel provider remains non-production because target acceptance is intentionally deferred. Its remaining blockers are:

- RTX/audio hardware acceptance in the integrated Divinus candidate;
- complete FH8626 candidate hardware acceptance.

These blockers are not software TODO markers and must only be removed from observed target evidence.

## Build and source checks

`tests/fh8626-check.sh` contains focused ABI/lifecycle regressions for fragile recovered contracts. These checks are secondary to the implementation itself and do not replace the target build.

The software-closure gate before camera testing is a successful OpenIPC ARM1176/musl build of the exact work-branch candidate. Camera execution/quality acceptance is intentionally a later phase.

## Deferred hardware acceptance

When hardware testing resumes, the candidate should be exercised for sensor/ISP bring-up, H.264 modes and 1080p scaling, RTSP/fMP4 reconnects, JPEG/MJPEG, OSD update/destroy/restart, DAY/NIGHT/DAY with anti-flicker and grayscale, RTX capture, and same-boot stop/start resource balance.

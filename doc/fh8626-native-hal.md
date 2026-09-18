# FH8626V100 native HAL status

This document tracks the current Divinus-native FH8626V100 implementation and its evidence boundary. It does not claim hardware acceptance of the latest candidate.

## Current architecture

FH8626V100 is a normal Divinus HAL platform (`HAL_PLATFORM_FH8626`). The former external `source: fh86` Unix/socket media-owner frontend has been removed. Divinus now owns the generic sensor/ISP/VPU/PAE/VENC path directly through `src/hal/full/`.

The OpenIPC FH8626 target is ARM1176JZF-S / ARMv6KZ soft-EABI on musl. For that target the native kernel provider is selected automatically at compile time. Runtime identification still requires the FH8626V100 machine identity plus the required media device nodes.

Board policy remains outside this HAL. Divinus does not contain the AJL33PQ0866 GPIO5 cold-reset sequence, GPIO4/GPIO14 lens policy, PTZ mapping, illumination policy or speaker-amplifier GPIO policy.

## Implemented platform contracts

The current source contains the recovered native contracts for:

- GC1054 1280x720 at 25 fps initialization/order;
- ISP context/MMIO initialization and corrected runtime statistics-bank ownership;
- VPU/PAE allocation, channel creation, bind and H.264 startup;
- H.264 CBR/VBR/QP/AVBR wire mapping used by cold startup;
- `MEDIA_STREAM_6` dequeue, ring-wrap handling and exactly-once `PAE_STREAM_STEP` release;
- owned-copy conversion to normal Divinus `hal_vidstream`;
- force-IDR through the recovered `FH_PAE_FORCE_I` operation;
- JPEG/MJPEG source implementation;
- RTX audio transport contract;
- best-effort teardown semantics;
- platform/system/media telemetry through `/api/status`.

The current provider reports its unresolved production blockers explicitly rather than hiding them behind a generic ready flag.

## Transitional dependencies

### GC1054 sensor plug-in

The current sensor backend still loads the exact V100-era `libmipi.so` and `libgc1054_mipi.so` callback ABI. This is a bring-up bridge, not the final OpenIPC-native sensor implementation. The clean target should eventually replace it with an open typed MIPI/GC1054 backend without importing opaque uClibc objects into the musl process.

### RTX audio capture

Divinus now owns the validated FH8626 RTX microphone capture transaction directly through `/dev/rtxbus`: reset, AC init, recovered initialization payload, capture NR configuration, 8 kHz/16-bit/mono/320-byte framing, AI enable/volume, shared-memory frame polling and AI disable on teardown. The former external `fh8626-audio` helper dependency is removed from the Divinus path.

This source integration is still target-unaccepted as a Divinus candidate. The provider therefore keeps a dedicated audio hardware-acceptance blocker. Speaker/amplifier GPIO policy is deliberately not part of this backend.

## Runtime reconfiguration boundary

Cold-start H.264 configuration is implemented. Live `/api/mp4` reconfiguration is deliberately rejected on FH8626V100 until the same-boot stop/reconfigure/restart transaction is hardware-accepted. This prevents the platform from falling through the generic channel lifecycle, which does not own FH8626 resources.

Force-IDR is independent of that restriction and is wired to the native PAE control path.

## Temperature

FH8626V100 temperature is explicitly unsupported. No RTC/TSENSOR/thermal-zone value is exposed as a SoC temperature without hardware evidence. `/api/status` reports:

- `temperature_available: false`;
- `temperature_c: null`;
- FH8626 temperature capability: `unsupported`.

## Observability

For FH8626V100, `/api/status` exposes:

- chip, family and platform identity;
- online CPU count;
- load averages;
- memory used/available/total with `sysinfo.mem_unit` applied;
- uptime;
- sensor identity;
- media backend and native-active state;
- enabled/main-loop channel counts and codec counts;
- provider blocker mask and production-ready state;
- evidence-class capability states.

## Host checks

Run the focused source/contract suite with:

`tests/fh8626-check.sh`

It covers contract, stub HAL, native adapter, lifecycle, provider boundary and stream-backend tests. Passing these checks is source validation only; it does not promote the latest native candidate to hardware acceptance.

## Current hardware gates

The next target run should prove the exact candidate end-to-end:

1. candidate identity and native provider selection;
2. GC1054/ISP bring-up and stable 720p25 H.264;
3. RTSP/raw H.264/fMP4 behavior and reconnect/force-IDR;
4. ISP exposure/color behavior;
5. JPEG/MJPEG only if enabled in the tested configuration;
6. native RTX microphone capture, independently from any board speaker/amplifier policy;
7. graceful stop and same-boot restart;
8. board lens/PTZ/illumination behavior through their owning board integration.

Only observed target evidence should remove the hardware-acceptance and teardown blockers.

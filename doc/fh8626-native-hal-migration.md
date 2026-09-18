# FH8626V100 native HAL migration

This file records the current migration boundary for FH8626V100 support in Divinus.

## Target shape

The target architecture is a normal native Divinus HAL:

`GC1054/MIPI -> ISP -> VPU -> PAE/VENC -> Divinus hal_vidstream -> RTSP/fMP4/recording`

There is no external FH86 encoded-stream protocol in the target design. The old `source: fh86` Unix/socket owner frontend and its wire/transport tests were removed after the native path became the active implementation line.

## Repository ownership

Divinus owns generic FH8626 streamer/HAL implementation:

- platform identification;
- FH media-device ownership;
- sensor/MIPI API boundary;
- ISP control;
- VPU/PAE/VENC/JPEG integration;
- stream dequeue/release;
- RTX audio integration boundary;
- streamer-facing telemetry and capability reporting.

Builder/device integration owns AJL33PQ0866-only behavior:

- GPIO5 cold-boot sensor reset/bootstrap;
- WIDE/TELE GPIO4/GPIO14 switching;
- PTZ/servomotor configuration;
- IR/white-light/IR-cut and other board GPIO policy;
- device deployment defaults.

Kernel code/patches remain Linux-owned. Shared runtime packaging remains Firmware-owned. Divinus contains no weak board-preparation hook.

## Current migration result

The native source now:

- owns the H.264 media pipeline directly;
- uses best-effort teardown instead of aborting cleanup on the first destructor error;
- exposes native force-IDR;
- reports platform/media/capability telemetry;
- marks FH8626 temperature unavailable;
- rejects unsafe live MP4 reconfiguration rather than entering the generic channel path;
- retains a stub provider for deterministic contract validation.

The provider is intentionally not production-ready. Its blocker mask describes actual remaining work:

- vendor GC1054/MIPI plug-in dependency;
- native RTX audio target acceptance;
- complete runtime video-reconfigure transaction;
- same-boot teardown/restart acceptance;
- latest-candidate hardware acceptance.

## Evidence distinctions

Recovered/replayed ABI contracts and host tests are not hardware acceptance. In particular:

- force-IDR is source/reverse-backed but still belongs in the next target regression;
- JPEG/MJPEG code exists but target acceptance remains unresolved;
- RTX transport is hardware-proven independently and is now integrated directly in Divinus, while the latest Divinus integration still needs target acceptance;
- full open sensor bring-up is unresolved because current native startup still loads vendor V100 sensor/MIPI objects;
- same-boot cleanup has stronger source behavior now, but the complete physical resource lifecycle still needs a target stop/restart run.

## Verification entry point

Focused host checks are grouped under:

`tests/fh8626-check.sh`

The authoritative next gate is an OpenIPC ARM1176/musl build of the exact work-branch commit followed by the physical-camera test matrix. The clean Firmware staging direction must consume that exact Divinus candidate rather than an unrelated moving upstream `HEAD`.

Do not restore the removed sidecar/owner architecture to work around a reproduced native bug. Reproduce the failure, fix the native owner or the correctly owning lower layer, and keep camera-specific policy outside Divinus.

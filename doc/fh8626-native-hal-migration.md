# FH8626V100 native HAL migration

This document is the implementation ledger for the native FH8626V100 HAL. The
goal is to make Divinus own the generic Fullhan media pipeline in the same
shape as the other upstream HALs. It is not a second camera profile and it is
not an external encoded-stream protocol.

## Baseline

The known-good ANJIA baseline is:

- FH8626V100;
- dual GC1054 sensors, 1280x720 at 25 fps;
- one H.264 channel, channel 0;
- baseline profile;
- CBR, 2048 kbps (`bitrate` in Divinus configuration is kilobits per second);
- ISP day profile with NR3D disabled by default;
- ONVIF, NAS recording and network streaming operating through the normal
  Divinus frontend.

The current firmware owner is the behavioral reference while this HAL is
implemented. Its low-level order is:

```text
sensor/MIPI -> ISP context/MMIO -> VPU system/channel -> ISP start
-> PAE system/channel -> MEDIA_BIND -> PAE encoder start -> VPU enable
-> MEDIA_STREAM_6 dequeue -> PAE_STREAM_STEP release
```

## Ownership boundary

Generic FH8626 media support belongs in `src/hal/full/`:

- device discovery and exclusive ownership;
- `/dev/isp`, `/dev/media_process`, `/dev/pae` and `/dev/vmm_userdev`;
- VMM allocations and mappings;
- sensor/MIPI driver contract;
- ISP initialization and runtime control;
- VPU/PAE lifecycle;
- H.264 configuration and frame dequeue/release;
- Divinus `hal_vidstream` delivery.

The Builder camera profile remains responsible for:

- selecting the active lens on a dual-sensor board;
- lens reset and board GPIO policy;
- PTZ and servomotor control;
- day/night/white-light policy specific to AJL33PQ0866;
- selecting the camera's ISP preset and deployment defaults.

No Unix `fh86` owner bridge is part of the target native path. The existing
bridge remains only as a temporary rollback/test path until native hardware
acceptance is complete.

## Evidence gates

The production provider may be enabled only after each gate has evidence on
the target camera:

1. exact device identity and exclusive ownership;
2. sensor/MIPI initialization and stable 720p25 frames;
3. ISP initialization with the known-good day preset;
4. VPU/PAE allocation, bind and encoder startup;
5. descriptor ring mapping, wrap handling and exactly-once release;
6. Divinus RTSP/fMP4/ONVIF/NAS behavior against the current baseline;
7. CBR target and runtime rate-control mapping;
8. force-IDR and reconnect behavior;
9. same-boot teardown and restart;
10. camera-specific lens/PTZ behavior after the native path is active.

Host tests prove contracts and failure paths only. They do not replace the
hardware gates above.

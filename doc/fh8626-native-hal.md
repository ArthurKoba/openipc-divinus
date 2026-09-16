# FH8626V100 native HAL status

This document tracks only evidence-backed FH8626V100 contracts. It does not claim physical validation of the Divinus native HAL.

## Proven foundation

- SoC/platform identity: FH8626V100; stock kernel reports `Machine: FH8626V100`.
- Native sensor geometry: dual GC1054, 1280x720 at 25 fps.
- Current proven encoded path: one H.264 channel at 1280x720/25, baseline profile, channel 0.
- Direct-kernel owner bring-up order is known for sensor -> ISP/VPU -> PAE -> media bind -> encoder start -> VPU enable.
- Encoded dequeue contract is known through `MEDIA_STREAM_6`; frame descriptor words 7/8 carry virtual address/length and word 10 carries an unresolved raw timestamp.
- Ring wrap handling is known and must occur before releasing the descriptor.
- Exactly-once descriptor release is required through `PAE_STREAM_STEP` after each acquired descriptor, including malformed/dropped frames.

## Deliberately unresolved

- Exact FH8626 encoder rate-control mode mapping from Divinus CBR/VBR/QP/AVBR to the recovered PAE configuration.
- Exact IDR request operation.
- Complete same-boot teardown contract for encoder/bind/VPU/ISP resources. Existing evidence gives ordering but not every destruction ioctl.
- Exact FH8626 VPSS 1280x720 -> 1920x1080 scaling/frame-control structures.
- Meaning/unit of descriptor timestamp word 10.
- Audio backend.
- Full native sensor bring-up without stock vendor shared objects.

## Upstream boundary

The eventual OpenIPC HAL must not depend on copied stock `.so` files or import FH8852/FH8852V201 structure layouts as FH8626 ABI. Current stock sensor plug-in helpers are reverse evidence only, not the intended upstream dependency.

Board-specific AJL33PQ0866 policy (dual-lens GPIO, GPIO5 cold reset, IR/white/IR-cut behavior) must remain separate from the generic FH8626 media HAL.

## Offline stream backend checkpoint

The native stream dequeue/release layer is implemented behind an injected ioctl boundary. It models the proven channel-0 `MEDIA_STREAM_6` acquisition and `PAE_STREAM_STEP` release contract without opening devices in the contract/backend layer itself. Tests cover contiguous and wrapped ring spans, no-frame responses, media errors, malformed descriptors with mandatory release, release retry after a failed step, and lifecycle preconditions. Physical target validation remains deferred while the camera is occupied by kernel/firmware work.

## Offline Divinus adapter checkpoint

The next layer copies an acquired native H.264 frame into owned scratch memory, releases the VPU/PAE descriptor, then converts the owned Annex-B frame into the normal Divinus `hal_vidpack`/`hal_vidstream` shape. The callback signature is identical to `save_video_stream`, so the physical backend can later attach to the existing media/RTSP path without retaining a hardware descriptor during downstream processing. Until the physical pipeline is available, fake-kernel tests exercise this path deterministically.

## Offline runtime orchestration

The native runtime controller is intentionally callback-driven. It encodes the proven Divinus-facing lifecycle order without acquiring devices directly, so it can be validated with a fake pipeline before physical media hardware is available. Startup is HAL -> system -> pipeline -> video -> stream. Shutdown is the exact reverse and refuses to proceed while an encoded-frame lease is outstanding.

This layer does not claim unresolved FH8626 ISP, IDR, rate-control, VPSS scaling, audio, or full device-open/mmap ABI. Those remain separate evidence gates.

## Native platform plumbing and bring-up modes

The native FH8626 platform is registered as `HAL_PLATFORM_FH8626`. The
`FH8626_NATIVE_KERNEL` build option routes the normal `sdk_start()` /
`sdk_stop()` entry points through the real device provider and identifies the
target from the machine marker plus required media device nodes. It is a
hardware bring-up option, not yet a release claim.

`FH8626_NATIVE_STUB` remains a compile-time validation mode. It selects the
FH8626 platform before ordinary hardware probing and routes the same entry
points through the fake kernel.

Stub mode exercises the same contract/backend/adapter path as the future kernel
backend.  Its fake kernel publishes a bounded Annex-B SPS/PPS/IDR access unit via
the recovered MEDIA_STREAM_6 / PAE_STREAM_STEP lease semantics.  The adapter
copies and releases the descriptor before calling the normal Divinus video sink,
so the RTSP path cannot retain a hardware lease.  Builds without
`FH8626_NATIVE_STUB` contain the plumbing but do not select the platform and do
not acquire FH8626 hardware.

## Production provider gate

The FH8626 platform exposes an explicit provider-status boundary. Stub mode is
selectable only when `FH8626_NATIVE_STUB` is compiled in. The kernel provider is
selectable only when `FH8626_NATIVE_KERNEL` is compiled in. The production gate
still reports blockers for exact chip detection, device-open/ring-mmap setup,
pipeline ownership, force-IDR, and runtime rate-control contracts. Until these
are proven on the camera, `fh8626_provider_production_ready()` remains false
even though the bring-up provider can be selected for testing.

## 2026-09-12 owner-parity correction

The control orchestration follows owner v4.3.0 around the shared algorithms:

- 6905 selects the completed descriptor bank and `isp_runtime.isp_cfg` is
  rebound to `allocation + 0x148770 + selected`;
- AWB/E2 statistics are consumed at selected runtime root `+0x48`, not from a
  fixed physical address when the alternate bank is active;
- the frontend synchronization barrier changes runtime-context bytes
  `ctx+0x18/+0x68`, not ISP MMIO registers at the same numeric offsets;
- unresolved ioctl `0x40046908` is not treated as a userspace 100-ms frame wait;
  the control loop uses the owner's absolute 40-ms monotonic schedule.

This corrects orchestration only. It does not alter the imported AWB, CCM, ISP
tables or NR3D policy, and it still requires target acceptance.

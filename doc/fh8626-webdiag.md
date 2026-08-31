# FH8626 Web diagnostics V11 candidate

Base checkpoint expected in the user's WSL Divinus branch: `74a797e` (`Add FH8626 live diagnostics web page`).

This is an Agent 6-only diagnostics hardening delta. It does not touch the FH8626 hardware owner, ISP, sensor, board, GPIO or Agent 7 source.

Changes relative to V9:

- browser preview starts disabled and is enabled only when `/api/live` reports fMP4 available;
- an active preview is stopped if fMP4 becomes unavailable;
- RTSP copy is disabled when RTSP is unavailable rather than constructing a fake usable URL;
- RTSP URL honors a capability-provided path when one exists and falls back to `/`;
- raw H.264 link follows the capability/path returned by `/api/live`;
- clipboard copy has a non-secure-HTTP fallback because the camera UI is normally served over plain HTTP;
- browser `play()` failure is surfaced and does not leave preview state falsely active;
- polling is guarded so a slow request cannot overlap the next 2-second refresh;
- failure of `/api/live` explicitly disables all live controls.

Build and test status: **PENDING_WSL**. Per project policy no host/ARM build or test result is claimed from the agent sandbox.

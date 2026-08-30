# FH8626 Web diagnostics V9

This checkpoint adds a self-contained FH8626 diagnostics page at `/fh8626`.
It does not alter the generic Divinus WebUI and does not add fake hardware
providers.

The page consumes the read-only backend contracts introduced by V7/V8:

- `/api/status`
- `/api/platform`
- `/api/fh86`
- `/api/live`

It displays source state/counters, platform identity, provider availability and
runtime status. Browser preview is opt-in and uses `/video.mp4` only when the
fMP4 endpoint is enabled. RTSP remains the preferred external-player path.

Temperature, GPIO, JPEG/MJPEG, audio and OSD continue to show `unavailable`
until real FH8626 providers exist. This checkpoint does not touch Agent 7,
ISP, sensor control, GPIO or board code.

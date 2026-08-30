# FH8626 live/backend diagnostics V8

This checkpoint adds read-only diagnostics for the external FH86 encoded-H.264 source.
It does not change sensor, ISP, GPIO, temperature, JPEG/MJPEG or other hardware ownership.

## `/api/fh86`

Reports the Divinus-side consumer state and recent frame telemetry:
- `state`: `stopped`, `waiting`, `streaming`, or `stalled`
- frames received and forwarded
- tiny frame count (`<128` bytes), exposed as telemetry only
- H.264 pack errors
- current wire generation and generation-change count
- last payload size, PTS and frame age

The endpoint is intentionally based on consumer observations. It does not claim that a
small encoded frame is black or that the upstream ISP is healthy.

## `/api/live`

Describes the live transports available to the WebUI for an FH86 source. The preferred
transport is RTSP; raw H.264 is always exposed by Divinus and fMP4 is exposed when MP4 is
enabled. Snapshot and MJPEG remain unavailable until a real provider exists.

This allows a later frontend checkpoint to select a supported live path instead of
assuming JPEG/MJPEG support.

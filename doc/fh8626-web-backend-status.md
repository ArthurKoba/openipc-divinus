# FH8626 external-source Web/API status boundary

This checkpoint improves the existing Divinus WebUI/API without pretending that
unimplemented FH8626 hardware providers already work.

For `source.type: fh86` Divinus now publishes the known target identity:

- chip: `FH8626V100`
- family: `fullhan-fh8626`
- sensor: `gc1054_mipi`

`/api/status` remains backwards compatible and gains `source` and `family`.
Unavailable temperature is rendered as `unavailable` instead of `nan°C`.

A new read-only endpoint `/api/platform` describes the current provider boundary.
Encoded H.264 is available; RTSP/fMP4 reflect configuration. Temperature, GPIO,
JPEG/MJPEG, audio and OSD remain explicitly unavailable in the external-source
stage until Agent7/native providers supply their contracts.

This does not alter the imaging/ISP owner and does not fake GPIO or temperature.
The current black camera image therefore remains an upstream imaging-runtime issue.

#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

for suite in \
    fh8626_contract \
    fh8626_graphv2 \
    fh8626_saturation \
    fh8626_hal_stub \
    fh8626_native_adapter \
    fh8626_native_runtime \
    fh8626_provider_boundary \
    fh8626_stream_backend \
    rtsp_thread \
    rtsp_transport
do
    make -C "$root/tests/$suite" clean
    make -C "$root/tests/$suite" test
done

echo "FH8626 host contract checks passed"

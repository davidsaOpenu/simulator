#!/usr/bin/env bash
# Build harness/qemu_mainloop_demo.c twice, inside the builder container (the host has no glib
# headers): bin/qemu_mainloop_demo_stock links the STOCK util/main-loop.c (git HEAD of the qemu
# tree) ahead of libqemuutil.a; bin/qemu_mainloop_demo_patched links the same file with
# scripts/qemu_main_loop_ns_deadline.patch applied. Neither depends on how libqemuutil.a was
# built, so this works on a fresh CI checkout as well as on a tree with the patch applied.
set -euo pipefail
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EVSSIM_ROOT="$(cd "$R/../.." && pwd)"
Q="$EVSSIM_ROOT/qemu"
IMAGE="${COSLEEP_IMAGE:-evssim:latest}"

[ -f "$Q/libqemuutil.a" ] || { echo "qemu is not built ($Q/libqemuutil.a missing): run compile-qemu.sh ubuntu-14.04" >&2; exit 2; }
mkdir -p "$R/bin" "$R/build"
git -C "$Q" show HEAD:util/main-loop.c > "$R/build/main-loop-stock.c"
cp "$R/build/main-loop-stock.c" "$R/build/main-loop-patched.c"
patch -s "$R/build/main-loop-patched.c" "$R/scripts/qemu_main_loop_ns_deadline.patch"
grep -q "qemu_aio_context->tlg" "$R/build/main-loop-patched.c" || { echo "patch did not apply" >&2; exit 2; }

CMD='set -e; Q=/evssim/qemu; F="-std=gnu99 -O2 -g -Wall -I$Q -I$Q/include -I$Q/tcg $(pkg-config --cflags glib-2.0 gthread-2.0) -D_GNU_SOURCE";
  L="$(pkg-config --libs glib-2.0 gthread-2.0) -lpthread -lrt -lm -lz";
  for v in stock patched; do
    gcc $F -c -o /work/build/main-loop-$v.o /work/build/main-loop-$v.c;
    gcc $F -o /work/bin/qemu_mainloop_demo_$v /work/harness/qemu_mainloop_demo.c /work/build/main-loop-$v.o $Q/libqemuutil.a $L;
  done'
docker run --rm --entrypoint /bin/bash -v "$EVSSIM_ROOT":/evssim -v "$R":/work -u "$(id -u):$(id -g)" "$IMAGE" -lc "$CMD"
make -s -C "$R" all     # host interference workers
ls -l "$R/bin/"

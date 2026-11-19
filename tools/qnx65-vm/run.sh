#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Boot QNX 6.5 for ARMv7 under QEMU (Cortex-A15 "virt" board), run commands
# in it and print what they wrote.
#
#   tools/qnx65-vm/run.sh -c "<command>" [-c ...] [-x "<regex>" ...] <file>...
#
# Each file goes into /proc/boot under its own name (on the PATH); the
# commands run in order from the boot script. Each -x pattern (extended
# regex) must appear in the serial log.
#
# QNX65_TOOLS     a QNX 6.5 ARMv7 toolchain (env.sh, mkifs, procnto, libc);
#                 default ~/src/tools/qnx-6.5sp1-tools (SP1, as in the car),
#                 else ~/src/tools/qnx-6.5-tools (6.5.0)
# QNX65_QEMU_DIR  the QEMU board pieces: startup-virt (QNX 6.5 QEMU BSP with
#                 the timer IRQ fix), libstartup.a, devc-serdebug, u-boot.bin
# QEMU_RUN_SECS   how long the commands may run in all (default 120)
# QEMU_KEEP_LOG   copy the serial log here
#
# Exit status: 0 when every runner-probe command printed "runner: PASS",
# none printed "runner: FAIL", and every -x pattern appeared.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
COMMANDS=""
EXPECT=""
NCMD=0
NRUNNER=0
while [ $# -gt 0 ] && { [ "$1" = "-c" ] || [ "$1" = "-x" ]; }; do
    if [ "$1" = "-x" ]; then
        EXPECT="$EXPECT$2
"
    else
        COMMANDS="$COMMANDS    $2
"
        NCMD=$((NCMD + 1))
        case "$2" in runner-probe*|*" runner-probe"*) NRUNNER=$((NRUNNER + 1)) ;; esac
    fi
    shift 2
done
[ "$NCMD" -gt 0 ] && [ $# -gt 0 ] || { echo "usage: run.sh -c <command>... <file>..." >&2; exit 2; }
if [ -z "${QNX65_TOOLS:-}" ]; then
    QNX65_TOOLS=$HOME/src/tools/qnx-6.5-tools
    [ -f "$HOME/src/tools/qnx-6.5sp1-tools/env.sh" ] && QNX65_TOOLS=$HOME/src/tools/qnx-6.5sp1-tools
fi
QNX65_QEMU_DIR=${QNX65_QEMU_DIR:-$HOME/src/reverse/Supra-iDrive6-Android-Auto/.qnx650/linux-toolchain-src/tests/qemu}
RUN_SECS=${QEMU_RUN_SECS:-120}
for f in startup-virt libstartup.a devc-serdebug u-boot.bin; do
    [ -f "$QNX65_QEMU_DIR/$f" ] || { echo "missing $QNX65_QEMU_DIR/$f" >&2; exit 2; }
done
command -v qemu-system-arm >/dev/null || { echo "qemu-system-arm is not installed" >&2; exit 2; }

WORK=$(mktemp -d)
QPID=""
FEED=""
trap '[ -n "$QPID" ] && kill "$QPID" "$FEED" 2>/dev/null; rm -rf "$WORK"' EXIT
mkdir "$WORK/files"
FILES=""
for f in "$@"; do
    cp "$f" "$WORK/files/"
    # Application payloads stay byte for byte (mkifs rewrites executables for
    # the boot image; the runner reads their notes as on the car).
    case "$f" in
        *.so|*.guest|*-bundle) FILES="$FILES[+raw] $(basename "$f")
" ;;
        *) FILES="$FILES$(basename "$f")
" ;;
    esac
done
cp "$QNX65_QEMU_DIR/startup-virt" "$QNX65_QEMU_DIR/libstartup.a" "$QNX65_QEMU_DIR/devc-serdebug" "$WORK/"

LIBC_DATE=$(strings "$QNX65_TOOLS/sdk/target/qnx6/armle-v7/lib/libc.so.3" 2>/dev/null | sed -n 's/^DATE=//p' | head -1)
echo "qnx65-vm: $QNX65_TOOLS (libc built ${LIBC_DATE:-?})" >&2

{
    sed -n '1,/@COMMANDS@/p' "$HERE/image.build" | sed -e '$d' -e "s|@WORK@|$WORK|g"
    printf '%s' "$COMMANDS"
    sed -n '/@COMMANDS@/,$p' "$HERE/image.build" | sed -e '1d' -e "s|@WORK@|$WORK|g"
    printf '%s' "$FILES"
} > "$WORK/image.build"

# The Screen stub, built with the toolchain (QNX 6.5 has no Screen).
bash -c '. "$1/env.sh" && qcc -Vgcc_ntoarmv7le -shared -Wl,-soname,libscreen.so.1 -o "$2/libscreen.so.1" "$3"' \
    _ "$QNX65_TOOLS" "$WORK" "$HERE/stub_screen.c" > "$WORK/stub.log" 2>&1 ||
    { echo "Screen stub failed:" >&2; cat "$WORK/stub.log" >&2; exit 1; }

(
    cd "$WORK"
    # env.sh is bash; it sets QNX_HOST, QNX_TARGET and PATH.
    bash -c '. "$1/env.sh" && T=$QNX_TARGET/armle-v7 &&
        MKIFS_PATH="$PWD:$PWD/files:$T/boot/sys:$T/bin:$T/lib:$T/usr/lib" mkifs image.build ifs.bin' \
        _ "$QNX65_TOOLS"
) > "$WORK/mkifs.log" 2>&1 || { echo "mkifs failed:" >&2; tail -10 "$WORK/mkifs.log" >&2; exit 1; }

mkdir -p "$WORK/fat"
mv "$WORK/ifs.bin" "$WORK/fat/"
LOG=$WORK/qemu.log
: > "$LOG"
mkfifo "$WORK/console"
# u-boot eats the first keystrokes for its autoboot prompt: newline, then pauses.
( printf '\n'; sleep 4
  printf 'virtio scan\n'; sleep 2
  printf 'fatload virtio 0:1 0x40200000 ifs.bin\n'; sleep 3
  printf 'go 0x40200000\n'; exec sleep $((RUN_SECS + 30)) ) > "$WORK/console" &
FEED=$!
timeout $((RUN_SECS + 30)) qemu-system-arm -M virt -m 512 -cpu cortex-a15 -smp 2 \
    -bios "$QNX65_QEMU_DIR/u-boot.bin" -drive file=fat:rw:"$WORK/fat",format=raw,media=disk \
    -display none -serial stdio < "$WORK/console" > "$LOG" 2>&1 &
QPID=$!
# Stop as soon as the script has finished.
i=0
while [ $i -lt $((RUN_SECS + 20)) ] && ! grep -q '@@NBT-END@@' "$LOG"; do
    sleep 1
    i=$((i + 1))
done
sleep 1
kill "$QPID" "$FEED" 2>/dev/null || true
pkill -P "$FEED" 2>/dev/null || true
wait "$QPID" "$FEED" 2>/dev/null || true
QPID=""

[ -n "${QEMU_KEEP_LOG:-}" ] && cp "$LOG" "$QEMU_KEEP_LOG"
if ! grep -q '@@NBT-START@@' "$LOG"; then
    echo "=== boot did not reach the commands ===" >&2
    tail -20 "$LOG" >&2
    exit 1
fi
awk '/@@NBT-START@@/ { sub(/.*@@NBT-START@@/, ""); inside = 1 }
     /@@NBT-END@@/ { sub(/@@NBT-END@@.*/, ""); if (length($0)) print; exit }
     inside && length($0) { print }' "$LOG" | tr -d '\r'
grep -oE 'Process [0-9]+ \([^)]*\) (terminated SIG[A-Z]+[^ ]*( [a-z]+=[0-9a-fx]+)*|exited status=[0-9]+)' "$LOG" | tail -5 >&2 || true
grep -q '@@NBT-END@@' "$LOG" || echo "qnx65-vm: commands did not finish within ${RUN_SECS}s" >&2
PASSES=$(grep -c 'runner: PASS' "$LOG" || true)
if grep -q 'runner: FAIL' "$LOG" || [ "$PASSES" -ne "$NRUNNER" ]; then
    echo "qnx65-vm: $PASSES of $NRUNNER runner commands passed" >&2
    exit 1
fi
MISSING=0
printf '%s' "$EXPECT" | while IFS= read -r pattern; do
    [ -n "$pattern" ] || continue
    tr -d '\r' < "$LOG" | grep -Eq -- "$pattern" || { echo "qnx65-vm: missing: $pattern" >&2; exit 1; }
done || MISSING=1
[ "$MISSING" -eq 0 ] || exit 1
echo "qnx65-vm: all $NRUNNER probe commands passed and every expected line appeared" >&2

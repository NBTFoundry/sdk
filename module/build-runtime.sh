#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Build the open runtime that NBT app modules link statically: picolibc (C
# library) and compiler-rt builtins (compiler helpers), for ARMv7-A softfp with
# int-size enums, position independent, emulated TLS. No QNX file is used.
#
#   module/build-runtime.sh <out-dir>
#
# LLVM_BIN (default /usr/lib/llvm-17/bin) holds clang and the llvm-* tools.
# MODULE_LD (default: ld.lld from LLVM_BIN, else arm-none-eabi-ld) links.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
OUT=${1:?usage: build-runtime.sh <out-dir>}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
LLVM_BIN=${LLVM_BIN:-/usr/lib/llvm-17/bin}
PATH=$LLVM_BIN:$PATH
export PATH
if [ -z "${MODULE_LD:-}" ]; then
    if [ -x "$LLVM_BIN/ld.lld" ]; then MODULE_LD=$LLVM_BIN/ld.lld
    else MODULE_LD=$(command -v arm-none-eabi-ld); fi
fi

# MODULE_UNALIGNED=1: allow unaligned accesses (see module/flags.mk).
ALIGN="-mno-unaligned-access"
[ "${MODULE_UNALIGNED:-0}" = 1 ] && ALIGN="-munaligned-access"
FLAGS="-marm -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp $ALIGN -fno-short-enums -fPIC -ffunction-sections -fdata-sections"
FLAGS_LIST=$(printf "'%s', " $FLAGS)

# Meson cross file: the linker path is only known here.
cat > "$OUT/cross-armv7a-nbt.txt" <<EOF
[binaries]
c = ['clang', '--target=armv7a-none-eabi', '-nostdlib']
c_ld = '$MODULE_LD'
ar = 'llvm-ar'
nm = 'llvm-nm'
strip = 'llvm-strip'

[host_machine]
system = 'none'
cpu_family = 'arm'
cpu = 'armv7a'
endian = 'little'

[built-in options]
c_args = [${FLAGS_LIST}'-femulated-tls']

[properties]
skip_sanity_check = true
libgcc = '-lclang_rt.builtins'
EOF

# --- picolibc ---
if [ ! -f "$OUT/picolibc/lib/libc.a" ]; then
    rm -rf "$OUT/picolibc-build"
    meson setup "$OUT/picolibc-build" "$ROOT/third_party/picolibc" \
        --cross-file "$OUT/cross-armv7a-nbt.txt" \
        --prefix "$OUT/picolibc" --libdir lib --includedir include \
        -Dbuildtype=release -Doptimization=2 \
        -Dmultilib=false -Dpicocrt=false -Dpicolib=false -Dsemihost=false \
        -Dtests=false -Dspecsdir=none \
        -Dthread-local-storage=true -Dsingle-thread=false \
        -Dposix-console=true -Dio-long-long=true -Dformat-default=double \
        -Derrno-function=false -Dnewlib-global-errno=false
    meson compile -C "$OUT/picolibc-build"
    meson install -C "$OUT/picolibc-build" --quiet
fi

# --- compiler-rt builtins ---
if [ ! -f "$OUT/builtins/libclang_rt.builtins.a" ]; then
    rm -rf "$OUT/builtins-build"
    cmake -S "$ROOT/third_party/llvm-project/compiler-rt/lib/builtins" -B "$OUT/builtins-build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_SYSTEM_PROCESSOR=arm \
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
        -DCMAKE_C_COMPILER=clang -DCMAKE_ASM_COMPILER=clang \
        -DCMAKE_C_COMPILER_TARGET=armv7-none-eabi -DCMAKE_ASM_COMPILER_TARGET=armv7-none-eabi \
        -DCMAKE_AR="$LLVM_BIN/llvm-ar" -DCMAKE_NM="$LLVM_BIN/llvm-nm" -DCMAKE_RANLIB="$LLVM_BIN/llvm-ranlib" \
        -DCMAKE_C_FLAGS="$FLAGS" -DCMAKE_ASM_FLAGS="$FLAGS" \
        -DCOMPILER_RT_BAREMETAL_BUILD=ON -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
        -DCOMPILER_RT_OS_DIR=. -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=OFF
    cmake --build "$OUT/builtins-build"
    mkdir -p "$OUT/builtins"
    find "$OUT/builtins-build" -name 'libclang_rt.builtins*.a' -exec cp {} "$OUT/builtins/libclang_rt.builtins.a" \;
fi
# --- libc++, libc++abi, libunwind (C++ for modules and guests) ---
# Static, over picolibc (which libc++ treats as newlib) and the module's own
# pthread.h. Exceptions use ARM EHABI tables found through the linker's
# __exidx_start/__exidx_end (bare-metal libunwind), so they never leave the
# module. libc++abi is built for a hosted system instead: its bare-metal
# variant jumps through an absolute address (a text relocation in a module)
# and reads typeinfo as PC-relative; the hosted one reads it through the GOT,
# which is why modules and guests link with --target2=got-rel.
if [ ! -f "$OUT/cxx/lib/libc++.a" ]; then
    rm -rf "$OUT/cxx-build"
    # picolibc hides POSIX under -std=c++NN and doesn't announce the clocks the
    # module runtime provides (module/runtime/sys.c): say so here.
    CXX_CFLAGS="$FLAGS -femulated-tls -nostdlibinc -I$ROOT/module/include -isystem $OUT/picolibc/include"
    CXX_CFLAGS="$CXX_CFLAGS -D_DEFAULT_SOURCE -D_POSIX_MONOTONIC_CLOCK=200112L -D_POSIX_TIMERS=200112L"
    CXX_CFLAGS="$CXX_CFLAGS -Dalloca=__builtin_alloca"
    cmake -S "$ROOT/third_party/llvm-project/runtimes" -B "$OUT/cxx-build" -G Ninja \
        -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_SYSTEM_PROCESSOR=arm \
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_ASM_COMPILER=clang \
        -DCMAKE_C_COMPILER_TARGET=armv7a-none-eabi -DCMAKE_CXX_COMPILER_TARGET=armv7a-none-eabi \
        -DCMAKE_ASM_COMPILER_TARGET=armv7a-none-eabi \
        -DCMAKE_AR="$LLVM_BIN/llvm-ar" -DCMAKE_NM="$LLVM_BIN/llvm-nm" -DCMAKE_RANLIB="$LLVM_BIN/llvm-ranlib" \
        -DCMAKE_C_FLAGS="$CXX_CFLAGS" -DCMAKE_CXX_FLAGS="$CXX_CFLAGS" -DCMAKE_ASM_FLAGS="$FLAGS" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_INSTALL_PREFIX="$OUT/cxx" \
        -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_DOCS=OFF \
        -DLIBCXX_ENABLE_SHARED=OFF -DLIBCXXABI_ENABLE_SHARED=OFF -DLIBUNWIND_ENABLE_SHARED=OFF \
        -DLIBCXX_CXX_ABI=libcxxabi -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON \
        -DLIBCXXABI_USE_LLVM_UNWINDER=ON -DLIBCXXABI_ENABLE_STATIC_UNWINDER=ON \
        -DLIBCXXABI_BAREMETAL=OFF -DLIBUNWIND_IS_BAREMETAL=ON \
        -DLIBCXX_ENABLE_THREADS=ON -DLIBCXX_HAS_PTHREAD_API=ON \
        -DLIBCXXABI_ENABLE_THREADS=ON -DLIBCXXABI_HAS_PTHREAD_API=ON \
        -DLIBUNWIND_ENABLE_THREADS=OFF -DLIBCXXABI_HAS_CXA_THREAD_ATEXIT_IMPL=OFF \
        -DLIBCXX_ENABLE_EXCEPTIONS=ON -DLIBCXXABI_ENABLE_EXCEPTIONS=ON -DLIBCXX_ENABLE_RTTI=ON \
        -DLIBCXX_ENABLE_FILESYSTEM=ON -DLIBCXX_ENABLE_RANDOM_DEVICE=OFF \
        -DLIBCXX_ENABLE_LOCALIZATION=ON -DLIBCXX_ENABLE_WIDE_CHARACTERS=ON \
        -DLIBCXX_ENABLE_MONOTONIC_CLOCK=ON \
        -DLIBCXX_USE_COMPILER_RT=ON -DLIBCXXABI_USE_COMPILER_RT=ON -DLIBUNWIND_USE_COMPILER_RT=ON \
        -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXX_INCLUDE_BENCHMARKS=OFF -DLIBCXXABI_INCLUDE_TESTS=OFF \
        -DLIBUNWIND_INCLUDE_TESTS=OFF -DLIBCXX_INCLUDE_DOCS=OFF -DLIBUNWIND_INCLUDE_DOCS=OFF
    cmake --build "$OUT/cxx-build"
    cmake --install "$OUT/cxx-build"
fi
echo "runtime: $OUT"

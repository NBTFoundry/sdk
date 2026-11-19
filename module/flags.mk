# SPDX-License-Identifier: MPL-2.0
# Building NBT application modules and guests (docs/native-apps.md): the one
# definition of their compiler and linker settings, used by the SDK's own
# rules (module/module.mk, guest/guest.mk) and by applications (nbt-sdk.mk).
# Before including, set:
#   NBT_MOD_ROOT    the SDK checkout (absolute)
#   NBT_MOD_BUILD   the SDK build directory (absolute)
# and optionally:
#   NBT_TOOLCHAIN        the NBT toolchain (default: the release below, where
#                        "make toolchain" in the SDK installs it)
#   NBT_MOD_UNALIGNED    1 to allow unaligned accesses (see below)

# The NBT toolchain (github.com/NBTFoundry/toolchains): clang, lld and the
# runtime every module and guest links statically (picolibc, compiler-rt,
# libc++), with the C library headers the module runtime implements.
NBT_TOOLCHAIN_VERSION := 1.0.0
NBT_TOOLCHAIN ?= $(NBT_MOD_ROOT)/toolchain/nbt-toolchain-$(NBT_TOOLCHAIN_VERSION)
NBT_LLVM_BIN := $(NBT_TOOLCHAIN)/bin
NBT_MOD_CC := $(NBT_LLVM_BIN)/clang
NBT_MOD_CXX := $(NBT_LLVM_BIN)/clang++
NBT_MOD_AR := $(NBT_LLVM_BIN)/llvm-ar
NBT_MOD_LD := $(NBT_LLVM_BIN)/ld.lld
NBT_MOD_READELF := $(NBT_LLVM_BIN)/llvm-readelf
NBT_PYTHON ?= python3

# The runtime comes in two variants. NBT_MOD_UNALIGNED=1 allows unaligned
# accesses: faster (picolibc's ARMv7-A memcpy needs them) but faulting on
# boards with strict alignment (the QEMU VM); whether the head unit allows
# them is a car-session result.
NBT_MOD_UNALIGNED ?= 0
NBT_MOD_SYSROOT := $(NBT_TOOLCHAIN)/armv7a-nbt
NBT_MOD_RUNTIME := $(NBT_MOD_SYSROOT)/$(if $(filter 1,$(NBT_MOD_UNALIGNED)),unaligned,aligned)
# What the runtime was compiled with, which code linking it must match:
# ARMv7-A, soft-float calling convention with VFPv3-D16, ARM state, int-size
# enums (NBT_TARGET_ARCH); PIC, emulated TLS (NBT_TARGET_CODEGEN); the POSIX
# features the module runtime provides (NBT_TARGET_DEFS).
-include $(NBT_MOD_RUNTIME)/target.mk
NBT_MOD_ARCH := $(NBT_TARGET_ARCH)

NBT_MOD_COMMON := $(NBT_MOD_ARCH) $(NBT_TARGET_CODEGEN) -fvisibility=hidden -ffunction-sections -fdata-sections
# Khronos headers without platform types.
NBT_MOD_DEFS := $(NBT_TARGET_DEFS) -DEGL_NO_PLATFORM_SPECIFIC_TYPES
NBT_MOD_SDK_INC := -I$(NBT_MOD_ROOT)/include -I$(NBT_MOD_ROOT)/third_party/OpenGL-Registry/api \
	-I$(NBT_MOD_ROOT)/third_party/EGL-Registry/api
NBT_MOD_LIBC_INC := -isystem $(NBT_MOD_SYSROOT)/include -isystem $(NBT_MOD_RUNTIME)/picolibc/include
NBT_MOD_C_INC := -nostdlibinc $(NBT_MOD_LIBC_INC) $(NBT_MOD_SDK_INC)
# libc++ wraps the C headers: its directory comes first.
NBT_MOD_CXX_INC := -nostdlibinc -nostdinc++ -isystem $(NBT_MOD_RUNTIME)/cxx/include/c++/v1 \
	$(NBT_MOD_LIBC_INC) $(NBT_MOD_SDK_INC)
NBT_MOD_CFLAGS := $(NBT_MOD_COMMON) $(NBT_MOD_DEFS) $(NBT_MOD_C_INC)
NBT_MOD_CXXFLAGS := $(NBT_MOD_COMMON) -fvisibility-inlines-hidden $(NBT_MOD_DEFS) $(NBT_MOD_CXX_INC)

# Modules: one export, only relative relocations, no loader-run constructors.
NBT_MOD_LDFLAGS := -shared -Bsymbolic --no-undefined --target2=got-rel -z text -z noexecstack \
	-z max-page-size=4096 --hash-style=both --gc-sections --build-id \
	--version-script $(NBT_MOD_ROOT)/module/exports.map -T $(NBT_MOD_ROOT)/module/module.ld \
	-u stdin -u stdout -u stderr
# Guests: static, at QNX's executable address, 4 KiB pages.
NBT_GUEST_LDFLAGS := -static -nostdlib -e _start --image-base=0x100000 --target2=got-rel \
	-z max-page-size=4096 -z common-page-size=4096 -z noexecstack --gc-sections --build-id \
	-T $(NBT_MOD_ROOT)/module/module.ld -u stdin -u stdout -u stderr
NBT_GUEST_RT_OBJS := $(addprefix $(NBT_MOD_BUILD)/obj/guest/,start.o host.o sync.o rpc.o media.o crt.o gl_stream.o gl_encode.o)

# The C and C++ runtime every module and guest links, and the SDK for them
# (front end and libretro host first: link order).
NBT_MOD_RT_LIBS := $(NBT_MOD_BUILD)/lib/libnbt-module-rt.a $(NBT_MOD_RUNTIME)/cxx/lib/libc++.a \
	$(NBT_MOD_RUNTIME)/cxx/lib/libunwind.a $(NBT_MOD_RUNTIME)/picolibc/lib/libc.a \
	$(NBT_MOD_RUNTIME)/builtins/libclang_rt.builtins.a
NBT_MOD_SDK_LIBS := $(addprefix $(NBT_MOD_BUILD)/lib/,libnbt-retro-mod.a libnbt-frontend-mod.a \
	libnbt-audio-mixer-mod.a libnbt-gles2-mod.a libnbt-sdk-mod.a libnbt-module-gl.a)
NBT_MOD_LINK_DEPS := $(NBT_MOD_SDK_LIBS) $(NBT_MOD_RT_LIBS) $(NBT_MOD_ROOT)/module/module.ld \
	$(NBT_MOD_ROOT)/module/exports.map
NBT_MOD_APP_LIBS := --start-group $(NBT_MOD_SDK_LIBS) $(NBT_MOD_RT_LIBS) --end-group

# Without the toolchain, say how to get it, when something needs it (a build
# without modules or guests does not).
nbt_mod_goal := $(.DEFAULT_GOAL)
$(filter $(NBT_MOD_RUNTIME)/%,$(NBT_MOD_RT_LIBS)):
	$(error No NBT toolchain $(NBT_TOOLCHAIN_VERSION) in $(NBT_TOOLCHAIN): run "make toolchain" in the SDK or set NBT_TOOLCHAIN (docs/native-apps.md))
.DEFAULT_GOAL := $(nbt_mod_goal)

# Payload checks: shape, ABI note, Thumb-state FPU code and, from the link
# map, licences (GPL/LGPL objects only in a guest).
NBT_MOD_CHECK := $(NBT_PYTHON) $(NBT_MOD_ROOT)/tools/check_module.py --readelf $(NBT_MOD_READELF) \
	--objdump $(NBT_LLVM_BIN)/llvm-objdump

# $(call nbt_module_link,output,objects,SPDX licence of the objects)
define nbt_module_link
	$(NBT_MOD_LD) $(NBT_MOD_LDFLAGS) -Map $(1).map -o $(1) $(2) $(NBT_MOD_APP_LIBS)
	$(NBT_MOD_CHECK) --map $(1).map --app-license "$(3)" $(1)
endef
# $(call nbt_guest_link,output,objects,SPDX licence of the objects)
define nbt_guest_link
	$(NBT_MOD_LD) $(NBT_GUEST_LDFLAGS) -Map $(1).map -o $(1) $(NBT_GUEST_RT_OBJS) $(2) $(NBT_MOD_APP_LIBS)
	$(NBT_MOD_CHECK) --map $(1).map --app-license "$(3)" $(1)
endef

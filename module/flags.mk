# SPDX-License-Identifier: MPL-2.0
# Building NBT application modules and guests (docs/native-apps.md): the one
# definition of their compiler and linker settings, used by the SDK's own
# rules (module/module.mk, guest/guest.mk) and by applications (nbt-sdk.mk).
# Before including, set:
#   NBT_MOD_ROOT    the SDK checkout (absolute)
#   NBT_MOD_BUILD   the SDK build directory (absolute)
# and optionally:
#   NBT_MOD_RUNTIME_DIR  where the runtime is built (default NBT_MOD_BUILD);
#                        it does not depend on the application
#   NBT_MOD_UNALIGNED    1 to allow unaligned accesses (see below)

NBT_LLVM_BIN ?= /usr/lib/llvm-17/bin
NBT_MOD_CC := $(NBT_LLVM_BIN)/clang
NBT_MOD_CXX := $(NBT_LLVM_BIN)/clang++
NBT_MOD_AR := $(NBT_LLVM_BIN)/llvm-ar
NBT_MOD_LD ?= $(firstword $(wildcard $(NBT_LLVM_BIN)/ld.lld) $(shell command -v arm-none-eabi-ld))
NBT_MOD_READELF ?= $(firstword $(shell command -v arm-none-eabi-readelf) $(NBT_LLVM_BIN)/llvm-readelf)
NBT_PYTHON ?= python3

# ARMv7-A, soft-float base calling convention, ARM state (QNX 6.5 kernels
# mishandle a thread's first FPU instruction in Thumb state), int-size enums.
# NBT_MOD_UNALIGNED=1 allows unaligned accesses: faster (picolibc's ARMv7-A
# memcpy needs them) but faulting on boards with strict alignment (the QEMU
# VM); whether the head unit allows them is a car-session result.
NBT_MOD_UNALIGNED ?= 0
ifeq ($(NBT_MOD_UNALIGNED),1)
NBT_MOD_ALIGN_FLAG := -munaligned-access
NBT_MOD_RUNTIME_NAME := module-runtime-unaligned
else
NBT_MOD_ALIGN_FLAG := -mno-unaligned-access
NBT_MOD_RUNTIME_NAME := module-runtime
endif
NBT_MOD_ARCH := --target=armv7a-none-eabi -marm -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp \
	$(NBT_MOD_ALIGN_FLAG) -fno-short-enums

# picolibc, compiler-rt and libc++ (module/build-runtime.sh).
NBT_MOD_RUNTIME_DIR ?= $(NBT_MOD_BUILD)
NBT_MOD_RUNTIME := $(NBT_MOD_RUNTIME_DIR)/$(NBT_MOD_RUNTIME_NAME)

NBT_MOD_COMMON := $(NBT_MOD_ARCH) -fPIC -femulated-tls -fvisibility=hidden -ffunction-sections -fdata-sections
# picolibc announces neither the clocks the module runtime provides nor POSIX
# under -std=c++NN; Khronos headers without platform types.
NBT_MOD_DEFS := -D_POSIX_MONOTONIC_CLOCK=200112L -D_POSIX_TIMERS=200112L -D_DEFAULT_SOURCE \
	-DEGL_NO_PLATFORM_SPECIFIC_TYPES
NBT_MOD_SDK_INC := -I$(NBT_MOD_ROOT)/include -I$(NBT_MOD_ROOT)/third_party/OpenGL-Registry/api \
	-I$(NBT_MOD_ROOT)/third_party/EGL-Registry/api
NBT_MOD_C_INC := -nostdlibinc -I$(NBT_MOD_ROOT)/module/include -isystem $(NBT_MOD_RUNTIME)/picolibc/include \
	$(NBT_MOD_SDK_INC)
# libc++ wraps the C headers: its directory comes first.
NBT_MOD_CXX_INC := -nostdlibinc -nostdinc++ -I$(NBT_MOD_ROOT)/module/include \
	-isystem $(NBT_MOD_RUNTIME)/cxx/include/c++/v1 -isystem $(NBT_MOD_RUNTIME)/picolibc/include $(NBT_MOD_SDK_INC)
NBT_MOD_CFLAGS := $(NBT_MOD_COMMON) $(NBT_MOD_DEFS) $(NBT_MOD_C_INC)
NBT_MOD_CXXFLAGS := $(NBT_MOD_COMMON) -fvisibility-inlines-hidden $(NBT_MOD_DEFS) $(NBT_MOD_CXX_INC)

# Modules: one export, only relative relocations, no loader-run constructors.
NBT_MOD_LDFLAGS := -shared -Bsymbolic --no-undefined --target2=got-rel -z text -z noexecstack \
	-z max-page-size=4096 --hash-style=both --gc-sections --build-id \
	--version-script $(NBT_MOD_ROOT)/module/exports.map -T $(NBT_MOD_ROOT)/module/module.ld \
	-u stdin -u stdout -u stderr
# Guests: static, at QNX's executable address, 4 KiB pages.
NBT_GUEST_LDFLAGS := -static -nostdlib -e _start -Ttext-segment=0x100000 --target2=got-rel \
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

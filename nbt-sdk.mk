# SPDX-License-Identifier: MPL-2.0
# Build and link the NBT App SDK from an application's Makefile:
#
#   NBT_SDK_DIR ?= ../sdk
#   NBT_SDK_BUILD_DIR := $(abspath build)/sdk
#   include $(NBT_SDK_DIR)/nbt-sdk.mk
#
#   CXXFLAGS += $(NBT_SDK_INC)
#   app-host: $(APP_OBJS) $(NBT_SDK_HOST_LIBS)
#   	$(CXX) $(APP_OBJS) $(NBT_SDK_HOST_LIBS) $(NBT_SDK_HOST_LDLIBS) -o $@
#
# The SDK is built by its own Makefile (targets libs-host, libs-asan,
# libs-qnx) into NBT_SDK_BUILD_DIR, the QNX libraries with the NBT toolchain.
# QNX_ENV and QNX_SCREEN_LIBDIR (the QNX SDP, only for nbt-runner) are passed on.

# The targets below must not become the including Makefile's default goal.
nbt_sdk_default_goal := $(.DEFAULT_GOAL)

NBT_SDK_DIR ?= ../sdk
override NBT_SDK_ROOT := $(abspath $(NBT_SDK_DIR))
NBT_SDK_BUILD_DIR ?= $(NBT_SDK_ROOT)/build
NBT_SDK_LIB_DIR = $(NBT_SDK_BUILD_DIR)/lib

NBT_SDK_INC := -I$(NBT_SDK_ROOT)/include

# Link order: the mixer uses the SDK; gles2 stands alone.
nbt_sdk_libs = $(NBT_SDK_LIB_DIR)/libnbt-audio-mixer-$(1).a $(NBT_SDK_LIB_DIR)/libnbt-gles2-$(1).a \
	$(NBT_SDK_LIB_DIR)/libnbt-sdk-$(1).a
NBT_SDK_HOST_LIBS = $(call nbt_sdk_libs,host)
NBT_SDK_ASAN_LIBS = $(call nbt_sdk_libs,asan)
NBT_SDK_QNX_LIBS = $(call nbt_sdk_libs,qnx)
# The front end (libnbt-frontend) and the libretro host (libnbt-retro), for
# applications that use them: put them first, the core (if any) before them.
nbt_sdk_front_libs = $(NBT_SDK_LIB_DIR)/libnbt-retro-$(1).a $(NBT_SDK_LIB_DIR)/libnbt-frontend-$(1).a \
	$(call nbt_sdk_libs,$(1))
NBT_SDK_FRONT_HOST_LIBS = $(call nbt_sdk_front_libs,host)
NBT_SDK_FRONT_ASAN_LIBS = $(call nbt_sdk_front_libs,asan)
NBT_SDK_FRONT_QNX_LIBS = $(call nbt_sdk_front_libs,qnx)
# libretro.h for an application's own sources (a core brings its own copy).
NBT_SDK_LIBRETRO_INC := -I$(NBT_SDK_ROOT)/third_party/libretro-common/include

NBT_SDK_HOST_LDLIBS := -lX11 -lEGL -lGLESv2 -lm -ldl -lpthread
# A QNX 6.5 program with the SDK, built without the QNX SDP (module/flags.mk):
#   app-qnx: $(APP_QNX_OBJS) $(NBT_SDK_QNX_LIBS)
#   	$(call nbt_qnx_link,$@,$(APP_QNX_OBJS) $(NBT_SDK_QNX_LIBS),$(NBT_SDK_QNX_LDLIBS))
# compiled with $(NBT_QNX_CXX) $(NBT_QNX_CXXFLAGS) $(NBT_QNX_SDK_INC). kdMain must
# be exported for libKD (see qnx/exports.list).
NBT_SDK_QNX_LDLIBS := --dynamic-list=$(NBT_SDK_ROOT)/qnx/exports.list -lscreen -lEGL -lGLESv2 -lsocket -lm

NBT_SDK_MAKE = $(MAKE) --no-print-directory -C $(NBT_SDK_ROOT) BUILD_DIR=$(NBT_SDK_BUILD_DIR) \
	QNX_ENV="$(QNX_ENV)" QNX_SCREEN_LIBDIR="$(QNX_SCREEN_LIBDIR)" NBT_TOOLCHAIN="$(NBT_TOOLCHAIN)" \
	NBT_MOD_UNALIGNED="$(NBT_MOD_UNALIGNED)"

.PHONY: nbt-sdk-host nbt-sdk-asan nbt-sdk-qnx
nbt-sdk-host:
	+@$(NBT_SDK_MAKE) libs-host
nbt-sdk-asan:
	+@$(NBT_SDK_MAKE) libs-asan
nbt-sdk-qnx:
	+@$(NBT_SDK_MAKE) libs-qnx

$(sort $(NBT_SDK_HOST_LIBS) $(NBT_SDK_FRONT_HOST_LIBS)): nbt-sdk-host ;
$(sort $(NBT_SDK_ASAN_LIBS) $(NBT_SDK_FRONT_ASAN_LIBS)): nbt-sdk-asan ;
$(sort $(NBT_SDK_QNX_LIBS) $(NBT_SDK_FRONT_QNX_LIBS)): nbt-sdk-qnx ;

# --- Without the QNX SDP: modules and guests, run by nbt-runner ---
# (docs/native-apps.md) Compile with $(NBT_MOD_CC) $(NBT_MOD_CFLAGS) or
# $(NBT_MOD_CXX) $(NBT_MOD_CXXFLAGS) plus the application's own flags, then
# link and check one of:
#   app.so: $(OBJS) $(NBT_SDK_MOD_DEPS)       a module (not GPL/LGPL)
#   	$(call nbt_module_link,$@,$(OBJS),<SPDX licence of OBJS>)
#   app.guest: $(OBJS) $(NBT_SDK_MOD_DEPS)    a guest (GPL/LGPL allowed)
#   	$(call nbt_guest_link,$@,$(OBJS),<SPDX licence of OBJS>)
NBT_MOD_ROOT := $(NBT_SDK_ROOT)
NBT_MOD_BUILD := $(abspath $(NBT_SDK_BUILD_DIR))
include $(NBT_SDK_ROOT)/module/flags.mk
NBT_SDK_MOD_DEPS = $(NBT_MOD_LINK_DEPS) $(NBT_GUEST_RT_OBJS)

# A test package's executable for today's NBT Manager: nbt-runner with the
# payload appended (built with the QNX SDP: needs QNX_ENV and QNX_SCREEN_LIBDIR).
#   $(NBT_SDK_BUNDLE) app.so app-bundle
NBT_SDK_RUNNER = $(NBT_SDK_BUILD_DIR)/nbt-runner
NBT_SDK_BUNDLE = $(NBT_PYTHON) $(NBT_SDK_ROOT)/tools/nbt_bundle.py $(NBT_SDK_RUNNER)
# Any payload appended to an application's executable, for a companion program
# the application starts itself (nbt/app/child.hpp):
#   $(NBT_SDK_APPEND) app companion app-with-companion
NBT_SDK_APPEND = $(NBT_PYTHON) $(NBT_SDK_ROOT)/tools/nbt_payload.py

.PHONY: nbt-sdk-runner nbt-sdk-mod
nbt-sdk-runner:
	+@$(NBT_SDK_MAKE) nbt-runner
$(NBT_SDK_RUNNER): nbt-sdk-runner ;
nbt-sdk-mod:
	+@$(NBT_SDK_MAKE) libs-mod guest-runtime
# (the toolchain's own files are not the SDK's to make: module/flags.mk)
$(filter-out $(NBT_MOD_SYSROOT)/%,$(NBT_SDK_MOD_DEPS)): nbt-sdk-mod ;

.DEFAULT_GOAL := $(nbt_sdk_default_goal)

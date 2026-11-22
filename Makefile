# SPDX-License-Identifier: MPL-2.0
SHELL := /bin/bash

BUILD_DIR ?= build
override BUILD_DIR := $(abspath $(BUILD_DIR))
OBJ_DIR := $(BUILD_DIR)/obj
LIB_DIR := $(BUILD_DIR)/lib
# Each kind of object rebuilds when its compiler or flags change (nbt-stamp.mk;
# the stamps are written at the end of this file).
include nbt-stamp.mk
stamp = $(OBJ_DIR)/$(1).stamp

# A QNX environment on PATH can set CC/CXX; host builds keep the host compiler.
ifeq ($(origin CC),default)
CC := gcc
endif
ifeq ($(origin CXX),default)
CXX := g++
endif
PYTHON ?= python3

# The QNX SDP is needed only for nbt-runner (runner/runner.mk), which ships
# with NBT Manager: QNX_ENV is its environment script to source. Everything
# else for the head unit builds with the NBT toolchain.
QNX_ENV ?=
QNX_SCREEN_LIBDIR ?=
QCC ?= qcc
QNX_AR ?= ntoarmv7-ar
QNX_READELF ?= ntoarmv7-readelf
QNX_NM ?= ntoarmv7-nm
QNX_STRIP ?= ntoarmv7-strip

# Only for check-readers: the manager tree the reader copies come from.
NBT_MGR_DIR ?= ../manager

INC := -Iinclude -Isrc -Ithird_party/libretro-common/include -Ithird_party/font8x8
WARN := -Wall -Wextra -Wno-unused-parameter
HOST_CFLAGS := -std=gnu99 -O2 $(WARN) $(CFLAGS)
HOST_CXXFLAGS := -std=c++11 -O2 $(WARN) -fno-exceptions -fno-rtti $(CXXFLAGS)
ASAN_FLAGS := -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
ASAN_CFLAGS := -std=gnu99 $(WARN) $(ASAN_FLAGS)
ASAN_CXXFLAGS := -std=c++11 $(WARN) -fno-exceptions -fno-rtti $(ASAN_FLAGS)
HOST_LDLIBS := -lX11 -lEGL -lGLESv2 -lm -ldl -lpthread

# nbt-runner (runner/runner.mk), with the QNX SDP, and the SDK built for it.
QNX_SDP_C_VARIANT := -Vgcc_ntoarmv7le
QNX_SDP_CPP_VARIANT := -Vgcc_ntoarmv7le_gpp
QNX_SDP_CFLAGS := $(QNX_SDP_C_VARIANT) -D_QNX_SOURCE -O2 -Wc,-std=gnu99 -Wc,-Wall -Wc,-Wextra $(INC)
QNX_SDP_CXXFLAGS := $(QNX_SDP_CPP_VARIANT) -D_QNX_SOURCE -O2 -Wc,-std=c++11 -Wc,-fno-exceptions \
	-Wc,-fno-rtti -Wc,-Wall -Wc,-Wextra $(INC)
QNX_SDP_DEPFLAGS = -Wp,-MMD,$(@:.o=.d) -Wp,-MP -Wp,-MT,$@
QNX_SDP_LDLIBS := -lscreen -lEGL -lGLESv2 -lsocket -lm

# QNX 6.5 programs: the NBT toolchain's QNX target (module/flags.mk).
QNX_CFLAGS = $(NBT_QNX_CFLAGS) -O2 -std=gnu99 $(WARN) $(INC) $(NBT_QNX_SDK_INC)
QNX_CXXFLAGS = $(NBT_QNX_CXXFLAGS) -O2 -std=c++11 $(WARN) -fno-exceptions -fno-rtti $(INC) $(NBT_QNX_SDK_INC)
# kdMain is exported: the car's libKD calls it.
QNX_LDLIBS := --dynamic-list=qnx/exports.list -lscreen -lEGL -lGLESv2 -lsocket -lm

# The bridge readers are verbatim copies of NBT Manager's (see READERS).
READER_FILES := \
	include/nbt/controller_wire.h:include/nbt/controller_wire.h \
	include/nbt/controller/input.hpp:include/nbt/controller/input.hpp \
	include/nbt/gamepad_wire.h:include/nbt/gamepad_wire.h \
	include/nbt/gamepad/input.hpp:include/nbt/gamepad/input.hpp \
	src/readers/controller_wire.cpp:src/sdk/controller_wire.cpp \
	src/readers/controller_input.cpp:src/sdk/controller_input.cpp \
	src/readers/gamepad_wire.cpp:src/sdk/gamepad_wire.cpp \
	src/readers/gamepad_input.cpp:src/sdk/gamepad_input.cpp \
	include/nbt/bridge_hold.h:include/nbt/bridge_hold.h \
	src/readers/bridge_hold.cpp:src/sdk/bridge_hold.cpp

SDK_SRC := $(sort $(wildcard src/*.cpp src/*.c src/readers/*.cpp))
MIXER_SRC := mixer/audio_mixer.cpp
GLES2_SRC := $(sort $(wildcard gles2/*.c))
FRONTEND_SRC := $(sort $(wildcard frontend/*.cpp frontend/*.c))
RETRO_SRC := $(sort $(wildcard retro/*.cpp))

objs = $(patsubst %.cpp,$(OBJ_DIR)/$(1)/%.o,$(filter %.cpp,$(2))) \
	$(patsubst %.c,$(OBJ_DIR)/$(1)/%.o,$(filter %.c,$(2)))
libs = $(LIB_DIR)/libnbt-audio-mixer-$(1).a $(LIB_DIR)/libnbt-gles2-$(1).a \
	$(LIB_DIR)/libnbt-sdk-$(1).a
# The front end and the libretro host, before the libraries above (link order).
front_libs = $(LIB_DIR)/libnbt-retro-$(1).a $(LIB_DIR)/libnbt-frontend-$(1).a

HOST_LIBS := $(call libs,host)
ASAN_LIBS := $(call libs,asan)
QNX_LIBS := $(call libs,qnx)

UNIT_TEST := $(BUILD_DIR)/sdk-unit-test
UNIT_ASAN := $(BUILD_DIR)/sdk-unit-asan
SMOKE := $(BUILD_DIR)/nbt-sdk-smoke
SMOKE_ASAN := $(BUILD_DIR)/nbt-sdk-smoke-asan
SMOKE_QNX := $(BUILD_DIR)/nbt-sdk-smoke-qnx
FRONTEND_TEST := $(BUILD_DIR)/sdk-frontend-test
FRONTEND_ASAN := $(BUILD_DIR)/sdk-frontend-asan
RETRO_APP := $(BUILD_DIR)/nbt-retro-fake
GL_LOOPBACK := $(BUILD_DIR)/sdk-gl-loopback
# The head unit's liba2i_client, faked for AudioPcm's A2I output (tests/sdk_test.cpp).
FAKE_A2I := $(BUILD_DIR)/fake/liba2i_client.so

.PHONY: all host libs-host libs-asan libs-qnx test test-asan check-static-init check-generic \
	check-readers qnx qnx-check qnx-env format clean

all: host

host: libs-host $(UNIT_TEST) $(SMOKE) $(FRONTEND_TEST) $(RETRO_APP) $(GL_LOOPBACK) $(FAKE_A2I)
libs-host: $(HOST_LIBS) $(call front_libs,host)
libs-asan: $(ASAN_LIBS) $(call front_libs,asan)
libs-qnx: $(QNX_LIBS) $(call front_libs,qnx)

# --- Objects ---
$(OBJ_DIR)/host/%.o: %.cpp $(call stamp,host)
	@mkdir -p $(dir $@)
	$(CXX) $(INC) $(HOST_CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/host/%.o: %.c $(call stamp,host)
	@mkdir -p $(dir $@)
	$(CC) $(INC) $(HOST_CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/asan/%.o: %.cpp $(call stamp,asan)
	@mkdir -p $(dir $@)
	$(CXX) $(INC) $(ASAN_CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/asan/%.o: %.c $(call stamp,asan)
	@mkdir -p $(dir $@)
	$(CC) $(INC) $(ASAN_CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/qnx/%.o: %.cpp $(call stamp,qnx)
	@mkdir -p $(dir $@)
	$(NBT_QNX_CXX) $(QNX_CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/qnx/%.o: %.c $(call stamp,qnx)
	@mkdir -p $(dir $@)
	$(NBT_QNX_CC) $(QNX_CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/qnx-sdp/%.o: %.cpp $(call stamp,qnx-sdp) | qnx-env
	@mkdir -p $(dir $@)
	. "$(QNX_ENV)" && $(QCC) $(QNX_SDP_CXXFLAGS) $(QNX_SDP_DEPFLAGS) -c $< -o $@

$(OBJ_DIR)/qnx-sdp/%.o: %.c $(call stamp,qnx-sdp) | qnx-env
	@mkdir -p $(dir $@)
	. "$(QNX_ENV)" && $(QCC) $(QNX_SDP_CFLAGS) $(QNX_SDP_DEPFLAGS) -c $< -o $@

# --- Libraries ---
define host_lib
$(LIB_DIR)/lib$(1)-$(2).a: $(call objs,$(2),$(3))
	@mkdir -p $$(dir $$@)
	rm -f $$@
	ar rcs $$@ $$^
endef
$(eval $(call host_lib,nbt-sdk,host,$(SDK_SRC)))
$(eval $(call host_lib,nbt-audio-mixer,host,$(MIXER_SRC)))
$(eval $(call host_lib,nbt-gles2,host,$(GLES2_SRC)))
$(eval $(call host_lib,nbt-frontend,host,$(FRONTEND_SRC)))
$(eval $(call host_lib,nbt-retro,host,$(RETRO_SRC)))
$(eval $(call host_lib,nbt-sdk,asan,$(SDK_SRC)))
$(eval $(call host_lib,nbt-audio-mixer,asan,$(MIXER_SRC)))
$(eval $(call host_lib,nbt-gles2,asan,$(GLES2_SRC)))
$(eval $(call host_lib,nbt-frontend,asan,$(FRONTEND_SRC)))
$(eval $(call host_lib,nbt-retro,asan,$(RETRO_SRC)))

define qnx_lib
$(LIB_DIR)/lib$(1)-qnx.a: $(call objs,qnx,$(2))
	@mkdir -p $$(dir $$@)
	rm -f $$@
	$$(NBT_MOD_AR) rcs $$@ $$(filter %.o,$$^)
endef
$(eval $(call qnx_lib,nbt-sdk,$(SDK_SRC)))
$(eval $(call qnx_lib,nbt-audio-mixer,$(MIXER_SRC)))
$(eval $(call qnx_lib,nbt-gles2,$(GLES2_SRC)))
$(eval $(call qnx_lib,nbt-frontend,$(FRONTEND_SRC)))
$(eval $(call qnx_lib,nbt-retro,$(RETRO_SRC)))

# The SDK for nbt-runner, with the QNX SDP.
$(LIB_DIR)/libnbt-sdk-qnx-sdp.a: $(call objs,qnx-sdp,$(SDK_SRC)) | qnx-env
	@mkdir -p $(dir $@)
	rm -f $@
	. "$(QNX_ENV)" && $(QNX_AR) rcs $@ $(filter %.o,$^)

# --- Host tests ---
$(FAKE_A2I): tests/fake_a2i_client.cpp include/nbt/app/audio_a2i.hpp $(call stamp,host)
	@mkdir -p $(dir $@)
	$(CXX) $(INC) $(HOST_CXXFLAGS) -fPIC -shared -fvisibility=hidden $< -o $@

$(UNIT_TEST): $(OBJ_DIR)/host/tests/sdk_test.o $(HOST_LIBS)
	$(CXX) $^ $(LDFLAGS) $(HOST_LDLIBS) -o $@

$(SMOKE): $(OBJ_DIR)/host/tests/smoke_app.o $(HOST_LIBS)
	$(CXX) $^ $(LDFLAGS) $(HOST_LDLIBS) -o $@

$(FRONTEND_TEST): $(OBJ_DIR)/host/tests/frontend_test.o $(call front_libs,host) $(HOST_LIBS)
	$(CXX) $^ $(LDFLAGS) $(HOST_LDLIBS) -o $@

$(FRONTEND_ASAN): $(OBJ_DIR)/asan/tests/frontend_test.o $(call front_libs,asan) $(ASAN_LIBS)
	$(CXX) $(ASAN_FLAGS) $^ $(LDFLAGS) $(HOST_LDLIBS) -o $@

# The libretro host with a fake core (tests/test_retro.py).
$(RETRO_APP): $(OBJ_DIR)/host/tests/retro_app.o $(OBJ_DIR)/host/tests/fake_core.o \
		$(call front_libs,host) $(HOST_LIBS)
	$(CXX) $^ $(LDFLAGS) $(HOST_LDLIBS) -o $@

$(UNIT_ASAN): $(OBJ_DIR)/asan/tests/sdk_test.o $(ASAN_LIBS)
	$(CXX) $(ASAN_FLAGS) $^ $(LDFLAGS) $(HOST_LDLIBS) -o $@

$(SMOKE_ASAN): $(OBJ_DIR)/asan/tests/smoke_app.o $(ASAN_LIBS)
	$(CXX) $(ASAN_FLAGS) $^ $(LDFLAGS) $(HOST_LDLIBS) -o $@

test: host
	NBT_TEST_FAKE_A2I=$(abspath $(FAKE_A2I)) $(UNIT_TEST)
	$(FRONTEND_TEST)
	$(GL_LOOPBACK)
	NBT_SDK_BUILD_DIR=$(BUILD_DIR) $(PYTHON) -m unittest discover -s tests -v
	@$(MAKE) --no-print-directory check-generic

test-asan: $(UNIT_ASAN) $(SMOKE_ASAN) $(FRONTEND_ASAN) $(FAKE_A2I)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		NBT_TEST_FAKE_A2I=$(abspath $(FAKE_A2I)) $(UNIT_ASAN)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 $(FRONTEND_ASAN)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		NBT_SDK_BUILD_DIR=$(BUILD_DIR) NBT_SDK_SMOKE=$(SMOKE_ASAN) \
		$(PYTHON) -m unittest discover -s tests

# No object may run code before main (.init_array/.ctors): QNX 6.5's loader
# and the manager's sandbox expect none.
check-static-init: libs-host
	@set -e; \
	for obj in $(call objs,host,$(SDK_SRC) $(MIXER_SRC) $(GLES2_SRC) $(FRONTEND_SRC) $(RETRO_SRC)); do \
		ctors="$$(readelf -S "$$obj" | awk '/\.init_array|\.ctors/ {print $$2}')"; \
		if test -n "$$ctors"; then \
			echo "ERROR: static constructor section in $$obj: $$ctors"; exit 1; \
		fi; \
	done; \
	echo "OK: no .init_array/.ctors in SDK objects"

# The SDK stays application-neutral and machine-neutral. The pattern is built
# from pieces so that this file does not match it. Links to the sibling
# repositories (the README's project row) are allowed.
GENERIC_DENY := $(subst _,,d_oom|pr_boom|/ho_me/)
check-generic:
	@if grep -rIinE '$(GENERIC_DENY)' --exclude-dir=build --exclude-dir=.git --exclude-dir=third_party \
		--exclude-dir=toolchain . \
		| grep -v 'https://github\.com/NBTFoundry/'; then \
		echo "ERROR: application- or machine-specific text in the files above"; exit 1; \
	fi; \
	echo "OK: SDK is application-neutral"

check-readers:
	@test -d "$(NBT_MGR_DIR)/src/sdk" || { echo "NBT_MGR_DIR=$(NBT_MGR_DIR) is not a manager tree"; exit 1; }
	@set -e; status=0; \
	for pair in $(READER_FILES); do \
		ours="$${pair%%:*}"; theirs="$(NBT_MGR_DIR)/$${pair#*:}"; \
		if ! cmp -s "$$ours" "$$theirs"; then echo "DIFFERS: $$ours vs $$theirs"; status=1; fi; \
	done; \
	test $$status -eq 0 && echo "OK: readers match $(NBT_MGR_DIR)"

# --- QNX 6.5 ARMv7: ordinary programs, with the NBT toolchain (no SDP) ---
qnx-env:
	@test -n "$(QNX_ENV)" || { echo "QNX_ENV is not set: make QNX_ENV=/path/to/qnx-env.sh ..."; exit 1; }
	@test -r "$(QNX_ENV)" || { echo "QNX_ENV=$(QNX_ENV) is not readable"; exit 1; }

$(SMOKE_QNX): $(OBJ_DIR)/qnx/tests/smoke_app.o $(QNX_LIBS) qnx/exports.list
	$(call nbt_qnx_link,$@,$(filter %.o %.a,$^),$(QNX_LDLIBS))

qnx: libs-qnx $(SMOKE_QNX)

# The car runs QNX 6.5.0 SP1: imports must exist there, not just in the 6.6 SDP.
QNX65_SP1_SYSROOT ?= $(HOME)/src/tools/qnx-6.5sp1-tools/sdk/target/qnx6
# qnx_import_check(binaries)
define qnx_import_check
	@if test -d "$(QNX65_SP1_SYSROOT)"; then \
		$(PYTHON) tools/check_qnx_imports.py --sysroot "$(QNX65_SP1_SYSROOT)" --readelf $(NBT_MOD_READELF) $(1); \
	else echo "skipped: no QNX 6.5.0 SP1 sysroot at $(QNX65_SP1_SYSROOT)"; fi
endef

qnx-check: qnx
	$(NBT_MOD_READELF) -h -d $(SMOKE_QNX)
	@$(NBT_LLVM_BIN)/llvm-nm -D --defined-only $(SMOKE_QNX) | grep -qw kdMain || { echo "kdMain is not exported"; exit 1; }
	@echo "OK: kdMain exported"
	$(call qnx_import_check,$(SMOKE_QNX))

# --- Housekeeping ---
FORMAT_FILES = $(filter-out src/readers/% $(foreach p,$(READER_FILES),$(firstword $(subst :, ,$(p)))), \
	$(shell find include src mixer gles2 frontend retro tests \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \)))

format:
	clang-format -i $(FORMAT_FILES)

clean:
	rm -rf $(BUILD_DIR)

DEPS := $(shell find $(OBJ_DIR) -name '*.d' 2>/dev/null)
-include $(DEPS)

# --- Applications without the QNX SDP (docs/native-apps.md) ---
include module/module.mk
include guest/guest.mk
include runner/runner.mk
include tools/qnx65-vm/vm.mk
include tests/loopback.mk

# The stamps, now that every flag is set.
$(call nbt_stamp,$(call stamp,host),$(CC) $(INC) $(HOST_CFLAGS) | $(CXX) $(INC) $(HOST_CXXFLAGS),$(CC) $(CXX))
$(call nbt_stamp,$(call stamp,asan),$(CC) $(INC) $(ASAN_CFLAGS) | $(CXX) $(INC) $(ASAN_CXXFLAGS),$(CC) $(CXX))
$(call nbt_stamp,$(call stamp,loopback),$(CC) $(LOOPBACK_CFLAGS) | $(CXX) $(INC) $(HOST_CXXFLAGS),$(CC) $(CXX))
$(call nbt_stamp,$(call stamp,qnx),$(NBT_QNX_CC) $(QNX_CFLAGS) | $(NBT_QNX_CXX) $(QNX_CXXFLAGS),,$(NBT_TOOLCHAIN)/VERSION)
$(call nbt_stamp,$(call stamp,qnx-sdp),$(if $(QNX_ENV),$(QNX_ENV) $(QCC) $(QNX_SDP_CFLAGS) | $(QNX_SDP_CXXFLAGS) \
	| $(RUNNER_FLAGS)))
$(call nbt_stamp,$(call stamp,module),$(NBT_MOD_CC) $(MODULE_CFLAGS) | $(MODULE_CXXFLAGS) | $(MOD_CFLAGS) \
	| $(MOD_CXXFLAGS) | $(GUEST_CFLAGS),,$(NBT_TOOLCHAIN)/VERSION)

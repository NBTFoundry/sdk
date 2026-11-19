SHELL := /bin/bash

BUILD_DIR ?= build
override BUILD_DIR := $(abspath $(BUILD_DIR))
OBJ_DIR := $(BUILD_DIR)/obj
LIB_DIR := $(BUILD_DIR)/lib

# A QNX environment on PATH can set CC/CXX; host builds keep the host compiler.
ifeq ($(origin CC),default)
CC := gcc
endif
ifeq ($(origin CXX),default)
CXX := g++
endif
PYTHON ?= python3

# QNX 6.5 cross toolchain: QNX_ENV is the environment script to source.
QNX_ENV ?=
QNX_SCREEN_LIBDIR ?=
QCC ?= qcc
QNX_AR ?= ntoarmv7-ar
QNX_READELF ?= ntoarmv7-readelf
QNX_NM ?= ntoarmv7-nm
QNX_STRIP ?= ntoarmv7-strip

# Only for check-readers: the manager tree the reader copies come from.
NBT_MGR_DIR ?= ../manager

INC := -Iinclude -Isrc
WARN := -Wall -Wextra -Wno-unused-parameter
HOST_CFLAGS := -std=gnu99 -O2 $(WARN) $(CFLAGS)
HOST_CXXFLAGS := -std=c++11 -O2 $(WARN) -fno-exceptions -fno-rtti $(CXXFLAGS)
ASAN_FLAGS := -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
ASAN_CFLAGS := -std=gnu99 $(WARN) $(ASAN_FLAGS)
ASAN_CXXFLAGS := -std=c++11 $(WARN) -fno-exceptions -fno-rtti $(ASAN_FLAGS)
HOST_LDLIBS := -lX11 -lEGL -lGLESv2 -lm -ldl -lpthread

QNX_C_VARIANT := -Vgcc_ntoarmv7le
QNX_CPP_VARIANT := -Vgcc_ntoarmv7le_gpp
QNX_CFLAGS := $(QNX_C_VARIANT) -D_QNX_SOURCE -O2 -Wc,-std=gnu99 -Wc,-Wall -Wc,-Wextra $(INC)
QNX_CXXFLAGS := $(QNX_CPP_VARIANT) -D_QNX_SOURCE -O2 -Wc,-std=c++11 -Wc,-fno-exceptions \
	-Wc,-fno-rtti -Wc,-Wall -Wc,-Wextra $(INC)
QNX_DEPFLAGS = -Wp,-MMD,$(@:.o=.d) -Wp,-MP -Wp,-MT,$@
QNX_LDLIBS := -lscreen -lEGL -lGLESv2 -lsocket -lm

# The bridge readers are verbatim copies of NBT Manager's (see READERS).
READER_FILES := \
	include/nbt/controller_wire.h:include/nbt/controller_wire.h \
	include/nbt/controller/input.hpp:include/nbt/controller/input.hpp \
	include/nbt/gamepad_wire.h:include/nbt/gamepad_wire.h \
	include/nbt/gamepad/input.hpp:include/nbt/gamepad/input.hpp \
	src/readers/controller_wire.cpp:src/sdk/controller_wire.cpp \
	src/readers/controller_input.cpp:src/sdk/controller_input.cpp \
	src/readers/gamepad_wire.cpp:src/sdk/gamepad_wire.cpp \
	src/readers/gamepad_input.cpp:src/sdk/gamepad_input.cpp

SDK_SRC := $(sort $(wildcard src/*.cpp src/readers/*.cpp))
MIXER_SRC := mixer/audio_mixer.cpp
GLES2_SRC := $(sort $(wildcard gles2/*.c))

objs = $(patsubst %.cpp,$(OBJ_DIR)/$(1)/%.o,$(filter %.cpp,$(2))) \
	$(patsubst %.c,$(OBJ_DIR)/$(1)/%.o,$(filter %.c,$(2)))
libs = $(LIB_DIR)/libnbt-audio-mixer-$(1).a $(LIB_DIR)/libnbt-gles2-$(1).a \
	$(LIB_DIR)/libnbt-sdk-$(1).a

HOST_LIBS := $(call libs,host)
ASAN_LIBS := $(call libs,asan)
QNX_LIBS := $(call libs,qnx)

UNIT_TEST := $(BUILD_DIR)/sdk-unit-test
UNIT_ASAN := $(BUILD_DIR)/sdk-unit-asan
SMOKE := $(BUILD_DIR)/nbt-sdk-smoke
SMOKE_ASAN := $(BUILD_DIR)/nbt-sdk-smoke-asan
SMOKE_QNX := $(BUILD_DIR)/nbt-sdk-smoke-qnx

.PHONY: all host libs-host libs-asan libs-qnx test test-asan check-static-init check-generic \
	check-readers qnx qnx-check qnx-env format clean

all: host

host: libs-host $(UNIT_TEST) $(SMOKE)
libs-host: $(HOST_LIBS)
libs-asan: $(ASAN_LIBS)
libs-qnx: $(QNX_LIBS)

# --- Objects ---
$(OBJ_DIR)/host/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(INC) $(HOST_CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/host/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(INC) $(HOST_CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/asan/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(INC) $(ASAN_CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/asan/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(INC) $(ASAN_CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/qnx/%.o: %.cpp | qnx-env
	@mkdir -p $(dir $@)
	. "$(QNX_ENV)" && $(QCC) $(QNX_CXXFLAGS) $(QNX_DEPFLAGS) -c $< -o $@

$(OBJ_DIR)/qnx/%.o: %.c | qnx-env
	@mkdir -p $(dir $@)
	. "$(QNX_ENV)" && $(QCC) $(QNX_CFLAGS) $(QNX_DEPFLAGS) -c $< -o $@

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
$(eval $(call host_lib,nbt-sdk,asan,$(SDK_SRC)))
$(eval $(call host_lib,nbt-audio-mixer,asan,$(MIXER_SRC)))
$(eval $(call host_lib,nbt-gles2,asan,$(GLES2_SRC)))

define qnx_lib
$(LIB_DIR)/lib$(1)-qnx.a: $(call objs,qnx,$(2)) | qnx-env
	@mkdir -p $$(dir $$@)
	rm -f $$@
	. "$(QNX_ENV)" && $(QNX_AR) rcs $$@ $$(filter %.o,$$^)
endef
$(eval $(call qnx_lib,nbt-sdk,$(SDK_SRC)))
$(eval $(call qnx_lib,nbt-audio-mixer,$(MIXER_SRC)))
$(eval $(call qnx_lib,nbt-gles2,$(GLES2_SRC)))

# --- Host tests ---
$(UNIT_TEST): $(OBJ_DIR)/host/tests/sdk_test.o $(HOST_LIBS)
	$(CXX) $^ $(LDFLAGS) $(HOST_LDLIBS) -o $@

$(SMOKE): $(OBJ_DIR)/host/tests/smoke_app.o $(HOST_LIBS)
	$(CXX) $^ $(LDFLAGS) $(HOST_LDLIBS) -o $@

$(UNIT_ASAN): $(OBJ_DIR)/asan/tests/sdk_test.o $(ASAN_LIBS)
	$(CXX) $(ASAN_FLAGS) $^ $(LDFLAGS) $(HOST_LDLIBS) -o $@

$(SMOKE_ASAN): $(OBJ_DIR)/asan/tests/smoke_app.o $(ASAN_LIBS)
	$(CXX) $(ASAN_FLAGS) $^ $(LDFLAGS) $(HOST_LDLIBS) -o $@

test: host
	$(UNIT_TEST)
	NBT_SDK_BUILD_DIR=$(BUILD_DIR) $(PYTHON) -m unittest discover -s tests -v
	@$(MAKE) --no-print-directory check-generic

test-asan: $(UNIT_ASAN) $(SMOKE_ASAN)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 $(UNIT_ASAN)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		NBT_SDK_BUILD_DIR=$(BUILD_DIR) NBT_SDK_SMOKE=$(SMOKE_ASAN) \
		$(PYTHON) -m unittest discover -s tests

# No object may run code before main (.init_array/.ctors): QNX 6.5's loader
# and the manager's sandbox expect none.
check-static-init: libs-host
	@set -e; \
	for obj in $(call objs,host,$(SDK_SRC) $(MIXER_SRC) $(GLES2_SRC)); do \
		ctors="$$(readelf -S "$$obj" | awk '/\.init_array|\.ctors/ {print $$2}')"; \
		if test -n "$$ctors"; then \
			echo "ERROR: static constructor section in $$obj: $$ctors"; exit 1; \
		fi; \
	done; \
	echo "OK: no .init_array/.ctors in SDK objects"

# The SDK stays application-neutral and machine-neutral. The pattern is built
# from pieces so that this file does not match it.
GENERIC_DENY := $(subst _,,d_oom|pr_boom|/ho_me/)
check-generic:
	@if grep -rIilE '$(GENERIC_DENY)' --exclude-dir=build --exclude-dir=.git .; then \
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

# --- QNX 6.5 ARMv7 ---
qnx-env:
	@test -n "$(QNX_ENV)" || { echo "QNX_ENV is not set: make QNX_ENV=/path/to/qnx-env.sh ..."; exit 1; }
	@test -r "$(QNX_ENV)" || { echo "QNX_ENV=$(QNX_ENV) is not readable"; exit 1; }

$(SMOKE_QNX): $(OBJ_DIR)/qnx/tests/smoke_app.o $(QNX_LIBS) qnx/exports.list | qnx-env
	@test -n "$(QNX_SCREEN_LIBDIR)" || { echo "QNX_SCREEN_LIBDIR is not set (directory with libscreen.so)"; exit 1; }
	. "$(QNX_ENV)" && $(QCC) $(QNX_CPP_VARIANT) -lang-c++ $(filter %.o %.a,$^) \
		-L$(QNX_SCREEN_LIBDIR) -Wl,--dynamic-list,qnx/exports.list $(QNX_LDLIBS) -o $@

qnx: libs-qnx $(SMOKE_QNX)

qnx-check: qnx
	. "$(QNX_ENV)" && $(QNX_READELF) -h -d $(SMOKE_QNX)
	. "$(QNX_ENV)" && set -e; \
		bad="$$($(QNX_NM) -u $(SMOKE_QNX) | grep -E 'GLIBCXX_3\.4\.(1[4-9]|[2-9][0-9])|__cxa_throw' || true)"; \
		if test -n "$$bad"; then echo "Disallowed QNX ABI symbols:"; echo "$$bad"; exit 1; fi; \
		$(QNX_NM) -D $(SMOKE_QNX) | grep -qw kdMain || { echo "kdMain is not exported"; exit 1; }; \
		echo "OK: QNX ELF/ABI symbols verified, kdMain exported"

# --- Housekeeping ---
FORMAT_FILES = $(filter-out src/readers/% $(foreach p,$(READER_FILES),$(firstword $(subst :, ,$(p)))), \
	$(shell find include src mixer gles2 tests \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \)))

format:
	clang-format -i $(FORMAT_FILES)

clean:
	rm -rf $(BUILD_DIR)

DEPS := $(shell find $(OBJ_DIR) -name '*.d' 2>/dev/null)
-include $(DEPS)

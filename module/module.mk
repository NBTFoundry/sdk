# SPDX-License-Identifier: MPL-2.0
# Applications without the QNX SDP (docs/native-apps.md): the module runtime
# library, the SDK built for modules and guests ("mod" libraries) and the test
# payloads, with the NBT toolchain. Included from the top Makefile; guests,
# the runner and the VM tests follow in guest/guest.mk, runner/runner.mk and
# tools/qnx65-vm/vm.mk.

NBT_MOD_ROOT := $(abspath .)
NBT_MOD_BUILD := $(BUILD_DIR)
include module/flags.mk

MODULE_RUNTIME_LIBS := $(NBT_MOD_RUNTIME)/picolibc/lib/libc.a $(NBT_MOD_RUNTIME)/builtins/libclang_rt.builtins.a \
	$(NBT_MOD_RUNTIME)/cxx/lib/libc++.a
MODULE_LIBC := $(firstword $(MODULE_RUNTIME_LIBS))
MODULE_RT := $(LIB_DIR)/libnbt-module-rt.a
MODULE_OBJ := $(OBJ_DIR)/module
MODULE_CFLAGS := $(NBT_MOD_CFLAGS) -O2 -std=gnu11 $(WARN) -Imodule/runtime
MODULE_CXXFLAGS := $(NBT_MOD_CXXFLAGS) -O2 -std=c++17 $(WARN) -Imodule/runtime

# --- The NBT toolchain release this SDK uses, into toolchain/ ---
# (module/toolchain.sha256 is that release's SHA256SUMS)
.PHONY: toolchain
toolchain:
	$(PYTHON) tools/get_toolchain.py $(NBT_TOOLCHAIN_VERSION) module/toolchain.sha256 toolchain

# --- The module runtime library: entry, system calls, threads, memcpy ---
# (the C library the toolchain's headers declare, over the host table)
# (wildcards sorted: archive member order, and so the payloads, must not
# depend on the file system's directory order)
MODULE_RT_OBJS := $(patsubst module/runtime/%.c,$(MODULE_OBJ)/rt/%.o,$(sort $(wildcard module/runtime/*.c)))

$(MODULE_OBJ)/rt/%.o: module/runtime/%.c $(MODULE_LIBC)
	@mkdir -p $(dir $@)
	$(NBT_MOD_CC) $(MODULE_CFLAGS) -MMD -MP -c $< -o $@

# memcpy's and string.c's loops must stay loops, not become calls to the
# functions they implement.
$(MODULE_OBJ)/rt/memcpy.o $(MODULE_OBJ)/rt/string.o: MODULE_CFLAGS += -fno-builtin

$(MODULE_RT): $(MODULE_RT_OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(NBT_MOD_AR) rcs $@ $^

# --- The SDK for modules and guests ("mod" libraries) ---
# The SDK's own sources, compiled with this toolchain; the platform files
# (window, audio device, bridge descriptors) are replaced by versions that ask
# the runner (module/sdk/). gl* calls go through a generated table.
MOD_OBJ := $(OBJ_DIR)/mod
MOD_CFLAGS := $(MODULE_CFLAGS) $(INC) -std=gnu99
MOD_CXXFLAGS := $(MODULE_CXXFLAGS) $(INC) -std=c++11 -fno-exceptions -fno-rtti
MOD_PLATFORM_SRC := src/lifecycle.cpp src/video_qnx.cpp src/video_egl.cpp src/audio_pcm.cpp src/child.cpp
MOD_SDK_SRC := $(filter-out $(MOD_PLATFORM_SRC),$(SDK_SRC)) $(sort $(wildcard module/sdk/*.cpp))
MOD_GL_C := $(BUILD_DIR)/module-gl/gl_table.c

$(MOD_OBJ)/%.o: %.cpp $(MODULE_RUNTIME_LIBS)
	@mkdir -p $(dir $@)
	$(NBT_MOD_CXX) $(MOD_CXXFLAGS) -MMD -MP -c $< -o $@

$(MOD_OBJ)/%.o: %.c $(MODULE_LIBC)
	@mkdir -p $(dir $@)
	$(NBT_MOD_CC) $(MOD_CFLAGS) -MMD -MP -c $< -o $@

$(MOD_GL_C): tools/gen_gl_table.py tools/glregistry.py third_party/OpenGL-Registry/xml/gl.xml
	@mkdir -p $(dir $@)
	$(PYTHON) tools/gen_gl_table.py third_party/OpenGL-Registry/xml/gl.xml $@

$(MOD_OBJ)/gl_table.o: $(MOD_GL_C) $(MODULE_LIBC)
	@mkdir -p $(dir $@)
	$(NBT_MOD_CC) $(MOD_CFLAGS) -Imodule/runtime -c $< -o $@

# mod_lib(name, objects)
define mod_lib
$(LIB_DIR)/libnbt-$(1).a: $(2)
	@mkdir -p $$(dir $$@)
	rm -f $$@
	$(NBT_MOD_AR) rcs $$@ $$^
endef
$(eval $(call mod_lib,sdk-mod,$(call objs,mod,$(MOD_SDK_SRC))))
$(eval $(call mod_lib,gles2-mod,$(call objs,mod,$(GLES2_SRC))))
$(eval $(call mod_lib,audio-mixer-mod,$(call objs,mod,$(MIXER_SRC))))
$(eval $(call mod_lib,frontend-mod,$(call objs,mod,$(FRONTEND_SRC))))
$(eval $(call mod_lib,retro-mod,$(call objs,mod,$(RETRO_SRC))))
$(eval $(call mod_lib,module-gl,$(MOD_OBJ)/gl_table.o))

.PHONY: libs-mod
libs-mod: $(NBT_MOD_SDK_LIBS) $(MODULE_RT)

# --- Test payloads, each as a module and as a guest ---
$(MODULE_OBJ)/tests/%.o: tests/module/%.c $(MODULE_LIBC)
	@mkdir -p $(dir $@)
	$(NBT_MOD_CC) $(MODULE_CFLAGS) -Itests/module -Icommon -MMD -MP -c $< -o $@

$(MODULE_OBJ)/tests/%.o: tests/module/%.cpp $(MODULE_RUNTIME_LIBS)
	@mkdir -p $(dir $@)
	$(NBT_MOD_CXX) $(MODULE_CXXFLAGS) -Itests/module -Icommon -MMD -MP -c $< -o $@

$(MODULE_OBJ)/common/%.o: common/%.c $(MODULE_LIBC)
	@mkdir -p $(dir $@)
	$(NBT_MOD_CC) $(MODULE_CFLAGS) -MMD -MP -c $< -o $@

# payload(name, objects): $(BUILD_DIR)/<name>.so and .guest, checked.
define payload
$(BUILD_DIR)/$(1).so: $(2) $(NBT_MOD_LINK_DEPS)
	$$(call nbt_module_link,$$@,$(2),MPL-2.0)
$(BUILD_DIR)/$(1).guest: $(2) $(NBT_GUEST_RT_OBJS) $(NBT_MOD_LINK_DEPS)
	$$(call nbt_guest_link,$$@,$(2),MPL-2.0)
endef
TEST_CHECK_OBJ := $(MODULE_OBJ)/tests/check.o
# The feasibility probe: C and C++ runtime, threads, files, JIT memory.
$(eval $(call payload,probe,$(MODULE_OBJ)/tests/probe.o $(MODULE_OBJ)/tests/probe_cxx.o $(TEST_CHECK_OBJ)))
# The car-session probe: GL, audio, throughput, NEON.
$(eval $(call payload,car-probe,$(MODULE_OBJ)/tests/car_probe.o $(TEST_CHECK_OBJ) \
	$(MODULE_OBJ)/common/membench.o))
# The SDK smoke application (the runner's lifecycle tests).
$(eval $(call payload,smoke,$(MOD_OBJ)/tests/smoke_app.o))

PROBE_MODULE := $(BUILD_DIR)/probe.so
PROBE_GUEST := $(BUILD_DIR)/probe.guest
CAR_PROBE := $(BUILD_DIR)/car-probe.so
CAR_PROBE_GUEST := $(BUILD_DIR)/car-probe.guest
SMOKE_MODULE := $(BUILD_DIR)/smoke.so
SMOKE_GUEST := $(BUILD_DIR)/smoke.guest

.PHONY: module-probe module-check car-probe smoke-module
module-probe module-check: $(PROBE_MODULE)
car-probe: $(CAR_PROBE) $(CAR_PROBE_GUEST)
smoke-module: $(SMOKE_MODULE) $(SMOKE_GUEST)

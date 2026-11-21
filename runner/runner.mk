# SPDX-License-Identifier: MPL-2.0
# The runners, built with the QNX SDP (QNX_ENV, QNX_SCREEN_LIBDIR):
#   nbt-runner        runs NBT applications built without the SDP; ships with
#                     NBT Manager
#   runner-probe-qnx  the same system services without the bridge, video or
#                     audio, for the feasibility probes in the QNX 6.5 VM
# Their own code is in ARM state: Thumb code in a guest died with SIGILL on
# the car, and in the QEMU VM the 6.5.0 and SP1 kernels mishandle a thread's
# first FPU instruction in Thumb state (SIGILL on 6.5.0, a hang on SP1). Included from the top Makefile after guest/guest.mk.

RUNNER_OBJ := $(OBJ_DIR)/qnx/runner
RUNNER_FLAGS := -Wc,-marm -Irunner -Icommon -I$(GL_STREAM_DIR) -include runner/gles2_qnx.h
# Shared by both runners: system services, module loading, the guest server
# and its GL decoder.
RUNNER_COMMON_OBJS := $(addprefix $(RUNNER_OBJ)/,host_sys.o module_loader.o guest_server.o gl_runner.o \
	gl_decode.o)
NBT_RUNNER := $(BUILD_DIR)/nbt-runner
NBT_RUNNER_OBJS := $(RUNNER_COMMON_OBJS) $(addprefix $(RUNNER_OBJ)/,nbt_runner.o fake_gl.o membench.o)
RUNNER_PROBE_QNX := $(BUILD_DIR)/runner-probe-qnx
RUNNER_PROBE_OBJS := $(RUNNER_COMMON_OBJS) $(RUNNER_OBJ)/runner_probe.o

# Static pattern rules: the QNX environment's make (3.81) takes the first
# matching implicit rule, the top Makefile's $(OBJ_DIR)/qnx/%.o, which lacks
# RUNNER_FLAGS; a static pattern rule wins in every make.
RUNNER_CXX_OBJS := $(RUNNER_OBJ)/nbt_runner.o
$(RUNNER_CXX_OBJS): $(RUNNER_OBJ)/%.o: runner/%.cpp $(GL_STREAM_GEN) | qnx-env
	@mkdir -p $(dir $@)
	. "$(QNX_ENV)" && $(QCC) $(QNX_CXXFLAGS) $(RUNNER_FLAGS) $(QNX_DEPFLAGS) -c $< -o $@

# The runners' C sources: their own, the shared benchmark, the generated decoders.
RUNNER_C_OBJS := $(addprefix $(RUNNER_OBJ)/,host_sys.o module_loader.o guest_server.o gl_runner.o \
	fake_gl.o runner_probe.o)
define runner_c
$(1): $(RUNNER_OBJ)/%.o: $(2)/%.c $(GL_STREAM_GEN) | qnx-env
	@mkdir -p $$(dir $$@)
	. "$$(QNX_ENV)" && $$(QCC) $$(QNX_CFLAGS) $$(RUNNER_FLAGS) $$(QNX_DEPFLAGS) -c $$< -o $$@
endef
$(eval $(call runner_c,$(RUNNER_C_OBJS),runner))
$(eval $(call runner_c,$(RUNNER_OBJ)/membench.o,common))
$(eval $(call runner_c,$(RUNNER_OBJ)/gl_decode.o,$(GL_STREAM_DIR)))

$(NBT_RUNNER): $(NBT_RUNNER_OBJS) $(LIB_DIR)/libnbt-sdk-qnx.a | qnx-env
	. "$(QNX_ENV)" && $(QCC) $(QNX_CPP_VARIANT) -lang-c++ $(NBT_RUNNER_OBJS) $(LIB_DIR)/libnbt-sdk-qnx.a \
		-L$(QNX_SCREEN_LIBDIR) $(QNX_LDLIBS) -o $@

$(RUNNER_PROBE_QNX): $(RUNNER_PROBE_OBJS) | qnx-env
	. "$(QNX_ENV)" && $(QCC) $(QNX_C_VARIANT) $(RUNNER_PROBE_OBJS) -o $@

.PHONY: nbt-runner runner-probe-qnx runner-check
nbt-runner: $(NBT_RUNNER)
runner-probe-qnx: $(RUNNER_PROBE_QNX)
# The runners run on the car: their imports must exist in QNX 6.5.0 SP1.
runner-check: $(NBT_RUNNER) $(RUNNER_PROBE_QNX)
	$(call qnx_import_check,$^)

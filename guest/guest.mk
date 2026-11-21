# SPDX-License-Identifier: MPL-2.0
# Mode B guests: static ARM executables with no QNX code in them, run by the
# runner as a separate process. Same compiler, runtime and application objects
# as modules (module/module.mk); the entry, the host table and the GL command
# stream differ. Included from the top Makefile after module.mk.

GUEST_OBJ := $(OBJ_DIR)/guest

# The GL command stream (include/nbt/gl_stream.h): generated encoders for the
# guest, generated decoders for the runner (runner/runner.mk).
GL_STREAM_DIR := $(BUILD_DIR)/gl-stream
GL_STREAM_GEN := $(addprefix $(GL_STREAM_DIR)/,gl_commands.h gl_encode.c gl_decode.c gl_functions.h \
	gl_redirect.h)
$(GL_STREAM_GEN) &: tools/gen_gl_stream.py tools/glregistry.py third_party/OpenGL-Registry/xml/gl.xml
	@mkdir -p $(GL_STREAM_DIR)
	$(PYTHON) tools/gen_gl_stream.py third_party/OpenGL-Registry/xml/gl.xml $(GL_STREAM_DIR)

GUEST_CFLAGS := $(MODULE_CFLAGS) -Iguest -I$(GL_STREAM_DIR)

$(GUEST_OBJ)/start.o: guest/start.S
	@mkdir -p $(dir $@)
	$(NBT_MOD_CC) $(NBT_MOD_ARCH) -c $< -o $@

$(GUEST_OBJ)/%.o: guest/%.c $(GL_STREAM_DIR)/gl_commands.h $(MODULE_LIBC)
	@mkdir -p $(dir $@)
	$(NBT_MOD_CC) $(GUEST_CFLAGS) -MMD -MP -c $< -o $@

# The module entry again, with the note saying "guest".
$(GUEST_OBJ)/crt.o: module/runtime/crt.c $(MODULE_LIBC)
	@mkdir -p $(dir $@)
	$(NBT_MOD_CC) $(MODULE_CFLAGS) -DNBT_MODULE_MODE=2 -MMD -MP -c $< -o $@

$(GUEST_OBJ)/gl_encode.o: $(GL_STREAM_DIR)/gl_encode.c $(MODULE_LIBC)
	@mkdir -p $(dir $@)
	$(NBT_MOD_CC) $(GUEST_CFLAGS) -c $< -o $@

.PHONY: guest-runtime guest-probe guest-check
guest-runtime: $(NBT_GUEST_RT_OBJS)
guest-probe guest-check: $(PROBE_GUEST)

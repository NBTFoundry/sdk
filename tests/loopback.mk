# SPDX-License-Identifier: MPL-2.0
# The guest GL stream against direct calls on the desktop's driver
# (tests/gl_loopback.cpp): each side's files linked into one object that keeps
# only its API global, as the guest and the runner are separate programs.
# Included from the top Makefile after guest/guest.mk.
LOOPBACK_OBJ := $(OBJ_DIR)/loopback
LOOPBACK_CFLAGS := $(HOST_CFLAGS) -g -Iinclude -I$(GL_STREAM_DIR)
# loopback_side(object, sources, include dir, kept symbols)
define loopback_side
	@mkdir -p $(dir $(1))
	$(CC) $(LOOPBACK_CFLAGS) -I$(3) -MMD -MP -MT $(1) -c $(word 1,$(2)) -o $(1:.o=-a.o)
	$(CC) $(LOOPBACK_CFLAGS) -I$(3) -MMD -MP -MT $(1) -c $(word 2,$(2)) -o $(1:.o=-b.o)
	ld -r $(1:.o=-a.o) $(1:.o=-b.o) -o $(1:.o=-ab.o)
	objcopy $(foreach s,$(4),--keep-global-symbol=$(s)) $(1:.o=-ab.o) $(1)
endef
$(LOOPBACK_OBJ)/guest.o: guest/gl_stream.c $(GL_STREAM_GEN) $(call stamp,loopback)
	$(call loopback_side,$@,guest/gl_stream.c $(GL_STREAM_DIR)/gl_encode.c,guest,nbtgl_attach nbtgl_proc nbtgl_take)
$(LOOPBACK_OBJ)/runner.o: runner/gl_runner.c $(GL_STREAM_GEN) $(call stamp,loopback)
	$(call loopback_side,$@,runner/gl_runner.c $(GL_STREAM_DIR)/gl_decode.c,runner,nbtgl_fn nbtgl_load nbtgl_run)
# The front end with its gl* calls through nbtgl_app.
$(LOOPBACK_OBJ)/%.o: frontend/%.cpp $(GL_STREAM_GEN) $(call stamp,loopback)
	@mkdir -p $(dir $@)
	$(CXX) $(INC) $(HOST_CXXFLAGS) -I$(GL_STREAM_DIR) -include gl_redirect.h -MMD -MP -c $< -o $@
$(LOOPBACK_OBJ)/font8x8.o: frontend/font8x8.c $(call stamp,loopback)
	@mkdir -p $(dir $@)
	$(CC) $(INC) $(HOST_CFLAGS) -MMD -MP -c $< -o $@
$(LOOPBACK_OBJ)/gl_loopback.o: tests/gl_loopback.cpp $(GL_STREAM_GEN) $(call stamp,loopback)
	@mkdir -p $(dir $@)
	$(CXX) $(INC) $(HOST_CXXFLAGS) -I$(GL_STREAM_DIR) -MMD -MP -c $< -o $@
$(GL_LOOPBACK): $(addprefix $(LOOPBACK_OBJ)/,gl_loopback.o frame_video.o overlay.o font8x8.o guest.o runner.o) \
		$(HOST_LIBS)
	$(CXX) $^ $(LDFLAGS) $(HOST_LDLIBS) -o $@

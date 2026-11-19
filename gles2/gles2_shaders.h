// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GLES2_SHADERS_H
#define NBT_GLES2_SHADERS_H

#include <GLES2/gl2.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  GLuint program;
  GLint u_mvp;
  GLint u_mv;
  GLint u_texmat;
  GLint u_tex;
  GLint u_tex_enabled;
  GLint u_texgen_enabled;
  GLint u_tex_env_mode;
  GLint u_alpha_test;
  GLint u_alpha_ref;
  GLint u_fog_enabled;
  GLint u_fog_color;
  GLint u_fog_density;
  GLint u_gamma;
} Gles2Program;

int gles2_shaders_init(Gles2Program* prog);
void gles2_shaders_destroy(Gles2Program* prog);

#ifdef __cplusplus
}
#endif

#endif

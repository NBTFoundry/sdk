// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GLES2_MATRIX_H
#define NBT_GLES2_MATRIX_H

#ifdef __cplusplus
extern "C" {
#endif

#define NBT_GL_MODELVIEW 0x1700
#define NBT_GL_PROJECTION 0x1701
#define NBT_GL_TEXTURE 0x1702

void gles2_mat_reset(void);
void gles2_mat_mode(unsigned int mode);
void gles2_mat_load_identity(void);
void gles2_mat_load(const float* m16);
void gles2_mat_push(void);
void gles2_mat_pop(void);
void gles2_mat_translate(float x, float y, float z);
void gles2_mat_scale(float x, float y, float z);
void gles2_mat_rotate(float angle_deg, float x, float y, float z);
void gles2_mat_ortho(float l, float r, float b, float t, float n, float f);

void gles2_mat_mul(float* out16, const float* a16, const float* b16);
const float* gles2_mat_top(unsigned int mode);
void gles2_mat_mvp(float* out16);

#ifdef __cplusplus
}
#endif

#endif

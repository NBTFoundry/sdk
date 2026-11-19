#include "nbt/gles2/gles2_matrix.h"

#include <math.h>
#include <string.h>

#define MV_STACK_CAP 32
#define PROJ_STACK_CAP 8
#define TEX_STACK_CAP 8

typedef struct {
  float m[32][16];
  int top;
  int cap;
} MatStack;

static MatStack g_mv;
static MatStack g_proj;
static MatStack g_tex;
static unsigned int g_mode = NBT_GL_MODELVIEW;
static int g_inited = 0;

static void set_identity(float* m) {
  memset(m, 0, sizeof(float) * 16);
  m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void ensure_init(void) {
  if (!g_inited) {
    gles2_mat_reset();
  }
}

static MatStack* active_stack(void) {
  ensure_init();
  if (g_mode == NBT_GL_PROJECTION) return &g_proj;
  if (g_mode == NBT_GL_TEXTURE) return &g_tex;
  return &g_mv;
}

void gles2_mat_reset(void) {
  g_inited = 1;
  g_mode = NBT_GL_MODELVIEW;
  g_mv.top = 0;
  g_mv.cap = MV_STACK_CAP;
  set_identity(g_mv.m[0]);
  g_proj.top = 0;
  g_proj.cap = PROJ_STACK_CAP;
  set_identity(g_proj.m[0]);
  g_tex.top = 0;
  g_tex.cap = TEX_STACK_CAP;
  set_identity(g_tex.m[0]);
}

void gles2_mat_mode(unsigned int mode) {
  ensure_init();
  if (mode == NBT_GL_MODELVIEW || mode == NBT_GL_PROJECTION || mode == NBT_GL_TEXTURE) {
    g_mode = mode;
  }
}

void gles2_mat_mul(float* out16, const float* a16, const float* b16) {
  float tmp[16];
  int c, r, k;
  for (c = 0; c < 4; c++) {
    for (r = 0; r < 4; r++) {
      float sum = 0.0f;
      for (k = 0; k < 4; k++) {
        sum += a16[k * 4 + r] * b16[c * 4 + k];
      }
      tmp[c * 4 + r] = sum;
    }
  }
  memcpy(out16, tmp, sizeof(tmp));
}

void gles2_mat_load_identity(void) {
  MatStack* s = active_stack();
  set_identity(s->m[s->top]);
}

void gles2_mat_load(const float* m16) {
  MatStack* s = active_stack();
  if (m16) memcpy(s->m[s->top], m16, sizeof(float) * 16);
}

void gles2_mat_push(void) {
  MatStack* s = active_stack();
  if (s->top + 1 < s->cap) {
    memcpy(s->m[s->top + 1], s->m[s->top], sizeof(float) * 16);
    s->top++;
  }
}

void gles2_mat_pop(void) {
  MatStack* s = active_stack();
  if (s->top > 0) s->top--;
}

void gles2_mat_translate(float x, float y, float z) {
  float t[16];
  MatStack* s = active_stack();
  set_identity(t);
  t[12] = x;
  t[13] = y;
  t[14] = z;
  gles2_mat_mul(s->m[s->top], s->m[s->top], t);
}

void gles2_mat_scale(float x, float y, float z) {
  float sc[16];
  MatStack* s = active_stack();
  set_identity(sc);
  sc[0] = x;
  sc[5] = y;
  sc[10] = z;
  gles2_mat_mul(s->m[s->top], s->m[s->top], sc);
}

void gles2_mat_rotate(float angle_deg, float x, float y, float z) {
  float len = sqrtf(x * x + y * y + z * z);
  float rad = angle_deg * (3.14159265358979323846f / 180.0f);
  float c = cosf(rad);
  float sn = sinf(rad);
  float ic = 1.0f - c;
  float r[16];
  MatStack* s = active_stack();
  if (len <= 1e-8f) return;
  x /= len;
  y /= len;
  z /= len;
  set_identity(r);
  r[0] = x * x * ic + c;
  r[1] = y * x * ic + z * sn;
  r[2] = z * x * ic - y * sn;
  r[4] = x * y * ic - z * sn;
  r[5] = y * y * ic + c;
  r[6] = z * y * ic + x * sn;
  r[8] = x * z * ic + y * sn;
  r[9] = y * z * ic - x * sn;
  r[10] = z * z * ic + c;
  gles2_mat_mul(s->m[s->top], s->m[s->top], r);
}

void gles2_mat_ortho(float l, float r, float b, float t, float n, float f) {
  float o[16];
  MatStack* s = active_stack();
  if (r == l || t == b || f == n) return;
  memset(o, 0, sizeof(o));
  o[0] = 2.0f / (r - l);
  o[5] = 2.0f / (t - b);
  o[10] = -2.0f / (f - n);
  o[12] = -(r + l) / (r - l);
  o[13] = -(t + b) / (t - b);
  o[14] = -(f + n) / (f - n);
  o[15] = 1.0f;
  gles2_mat_mul(s->m[s->top], s->m[s->top], o);
}

const float* gles2_mat_top(unsigned int mode) {
  ensure_init();
  if (mode == NBT_GL_PROJECTION) return g_proj.m[g_proj.top];
  if (mode == NBT_GL_TEXTURE) return g_tex.m[g_tex.top];
  return g_mv.m[g_mv.top];
}

void gles2_mat_mvp(float* out16) {
  ensure_init();
  gles2_mat_mul(out16, g_proj.m[g_proj.top], g_mv.m[g_mv.top]);
}

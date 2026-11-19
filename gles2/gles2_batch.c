#define NBT_GLES2_IMPL
#include "nbt/gles2/gles2_compat.h"
#include "nbt/gles2/gles2_matrix.h"
#include "gles2_shaders.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  float x, y, z;
  float u, v;
  float r, g, b, a;
} BatchVertex;

typedef struct {
  int enabled;
  GLint size;
  GLenum type;
  GLsizei stride;
  const void* ptr;
} ArrayBinding;

static Gles2Program g_prog;
static int g_prog_ready = 0;

static BatchVertex* g_verts = NULL;
static size_t g_verts_cap = 0;
static size_t g_verts_count = 0;
static unsigned short* g_indices = NULL;
static size_t g_indices_cap = 0;

static int g_in_begin = 0;
static GLenum g_begin_mode = GL_TRIANGLES;

static float g_cur_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
static float g_cur_tc[2] = {0.0f, 0.0f};

static int g_tex_enabled = 0;
static int g_texgen_bits = 0;
static int g_alpha_test = 0;
static GLenum g_alpha_func = GL_GEQUAL;
static float g_alpha_ref = 0.5f;
static int g_fog_enabled = 0;
static float g_fog_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
static float g_fog_density = 0.0f;
static float g_gamma = 1.0f;

static int g_tex_env_base = GL_MODULATE;
static int g_comb_rgb = GL_MODULATE;
static int g_comb_alpha = GL_MODULATE;
static int g_src0_rgb = GL_TEXTURE;
static int g_op0_rgb = GL_SRC_COLOR;

static ArrayBinding g_va = {0, 3, GL_FLOAT, 0, NULL};
static ArrayBinding g_ta = {0, 2, GL_FLOAT, 0, NULL};
static ArrayBinding g_ca = {0, 4, GL_FLOAT, 0, NULL};

static int compute_tex_env_mode(void) {
  if (g_tex_env_base == GL_DECAL) return 5;
  if (g_tex_env_base == GL_REPLACE) return 6;
  if (g_tex_env_base != GL_COMBINE) return 0;
  if (g_comb_rgb == GL_REPLACE && g_src0_rgb == GL_PRIMARY_COLOR) return 1; /* TM_MASK */
  if (g_comb_rgb == GL_MODULATE && g_comb_alpha == GL_REPLACE) {
    return (g_op0_rgb == GL_ONE_MINUS_SRC_COLOR) ? 4 : 2; /* INVERTOPAQUE : OPAQUE */
  }
  if (g_comb_rgb == GL_MODULATE && g_comb_alpha == GL_MODULATE &&
      g_op0_rgb == GL_ONE_MINUS_SRC_COLOR) {
    return 3; /* TM_INVERT */
  }
  return 0;
}

static int compute_alpha_mode(void) {
  if (!g_alpha_test || g_alpha_func == GL_ALWAYS) return 0;
  if (g_alpha_func == GL_GREATER) return 2;
  return 1; /* GEQUAL */
}

static int ensure_shader(void) {
  if (!g_prog_ready) {
    g_prog_ready = gles2_shaders_init(&g_prog);
  }
  return g_prog_ready;
}

int gles2_batch_init(void) {
  return ensure_shader();
}

static int upload_uniforms(void) {
  float mvp[16];
  if (!ensure_shader()) return 0;
  glUseProgram(g_prog.program);
  gles2_mat_mvp(mvp);
  glUniformMatrix4fv(g_prog.u_mvp, 1, GL_FALSE, mvp);
  glUniformMatrix4fv(g_prog.u_mv, 1, GL_FALSE, gles2_mat_top(NBT_GL_MODELVIEW));
  glUniformMatrix4fv(g_prog.u_texmat, 1, GL_FALSE, gles2_mat_top(NBT_GL_TEXTURE));
  glUniform1i(g_prog.u_tex, 0);
  glUniform1i(g_prog.u_tex_enabled, g_tex_enabled ? 1 : 0);
  glUniform1i(g_prog.u_texgen_enabled, (g_texgen_bits & 7) == 7 ? 1 : 0);
  glUniform1i(g_prog.u_tex_env_mode, compute_tex_env_mode());
  glUniform1i(g_prog.u_alpha_test, compute_alpha_mode());
  glUniform1f(g_prog.u_alpha_ref, g_alpha_ref);
  glUniform1i(g_prog.u_fog_enabled, g_fog_enabled ? 1 : 0);
  glUniform4fv(g_prog.u_fog_color, 1, g_fog_color);
  glUniform1f(g_prog.u_fog_density, g_fog_density);
  glUniform1f(g_prog.u_gamma, g_gamma);
  return 1;
}

int gles2_expand_quad_indices(int vertex_count, unsigned short* out_indices, int max_indices) {
  int quads = vertex_count / 4;
  int need = quads * 6;
  int i;
  if (quads <= 0 || need > max_indices || !out_indices) return 0;
  for (i = 0; i < quads; i++) {
    unsigned short base = (unsigned short)(i * 4);
    out_indices[i * 6 + 0] = base;
    out_indices[i * 6 + 1] = (unsigned short)(base + 1);
    out_indices[i * 6 + 2] = (unsigned short)(base + 2);
    out_indices[i * 6 + 3] = base;
    out_indices[i * 6 + 4] = (unsigned short)(base + 2);
    out_indices[i * 6 + 5] = (unsigned short)(base + 3);
  }
  return need;
}

static unsigned short* ensure_quad_indices(int vertex_count, int* out_count) {
  size_t need = (size_t)(vertex_count / 4) * 6u;
  if (need == 0) {
    *out_count = 0;
    return NULL;
  }
  if (need > g_indices_cap) {
    unsigned short* p = (unsigned short*)realloc(g_indices, need * sizeof(unsigned short));
    if (!p) {
      *out_count = 0;
      return NULL;
    }
    g_indices = p;
    g_indices_cap = need;
  }
  *out_count = gles2_expand_quad_indices(vertex_count, g_indices, (int)g_indices_cap);
  return g_indices;
}

static void push_vertex(float x, float y, float z) {
  if (!g_in_begin) return;
  if (g_verts_count >= g_verts_cap) {
    size_t nc = g_verts_cap ? g_verts_cap * 2u : 256u;
    BatchVertex* nv = (BatchVertex*)realloc(g_verts, nc * sizeof(BatchVertex));
    if (!nv) return;
    g_verts = nv;
    g_verts_cap = nc;
  }
  g_verts[g_verts_count].x = x;
  g_verts[g_verts_count].y = y;
  g_verts[g_verts_count].z = z;
  g_verts[g_verts_count].u = g_cur_tc[0];
  g_verts[g_verts_count].v = g_cur_tc[1];
  g_verts[g_verts_count].r = g_cur_color[0];
  g_verts[g_verts_count].g = g_cur_color[1];
  g_verts[g_verts_count].b = g_cur_color[2];
  g_verts[g_verts_count].a = g_cur_color[3];
  g_verts_count++;
}

void nbt_glBegin(GLenum mode) {
  g_in_begin = 1;
  g_begin_mode = mode;
  g_verts_count = 0;
}

void nbt_glEnd(void) {
  GLenum mode;
  if (!g_in_begin) return;
  g_in_begin = 0;
  if (g_verts_count == 0) return;

  if (!upload_uniforms()) {
    g_verts_count = 0;
    return;
  }
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), &g_verts[0].x);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), &g_verts[0].u);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), &g_verts[0].r);

  mode = g_begin_mode;
  if (mode == GL_QUAD_STRIP) mode = GL_TRIANGLE_STRIP;
  if (mode == GL_POLYGON) mode = GL_TRIANGLE_FAN;

  if (mode == GL_QUADS) {
    int ic = 0;
    unsigned short* idx = ensure_quad_indices((int)g_verts_count, &ic);
    if (idx && ic > 0) glDrawElements(GL_TRIANGLES, ic, GL_UNSIGNED_SHORT, idx);
  } else {
    glDrawArrays(mode, 0, (GLsizei)g_verts_count);
  }
  g_verts_count = 0;
}

void nbt_glVertex2f(GLfloat x, GLfloat y) {
  push_vertex(x, y, 0.0f);
}
void nbt_glVertex2i(GLint x, GLint y) {
  push_vertex((float)x, (float)y, 0.0f);
}
void nbt_glVertex3f(GLfloat x, GLfloat y, GLfloat z) {
  push_vertex(x, y, z);
}
void nbt_glVertex3fv(const GLfloat* v) {
  if (v) push_vertex(v[0], v[1], v[2]);
}
void nbt_glTexCoord2f(GLfloat s, GLfloat t) {
  g_cur_tc[0] = s;
  g_cur_tc[1] = t;
}
void nbt_glTexCoord2fv(const GLfloat* v) {
  if (v) {
    g_cur_tc[0] = v[0];
    g_cur_tc[1] = v[1];
  }
}
void nbt_glColor3f(GLfloat r, GLfloat g, GLfloat b) {
  g_cur_color[0] = r;
  g_cur_color[1] = g;
  g_cur_color[2] = b;
  g_cur_color[3] = 1.0f;
}
void nbt_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
  g_cur_color[0] = r;
  g_cur_color[1] = g;
  g_cur_color[2] = b;
  g_cur_color[3] = a;
}
void nbt_glColor4fv(const GLfloat* v) {
  if (v) memcpy(g_cur_color, v, sizeof(float) * 4);
}
void nbt_glColor4ubv(const GLubyte* v) {
  if (v) {
    g_cur_color[0] = (float)v[0] / 255.0f;
    g_cur_color[1] = (float)v[1] / 255.0f;
    g_cur_color[2] = (float)v[2] / 255.0f;
    g_cur_color[3] = (float)v[3] / 255.0f;
  }
}

void nbt_glMatrixMode(GLenum mode) {
  gles2_mat_mode(mode);
}
void nbt_glLoadIdentity(void) {
  gles2_mat_load_identity();
}
void nbt_glLoadMatrixf(const GLfloat* m) {
  gles2_mat_load(m);
}
void nbt_glPushMatrix(void) {
  gles2_mat_push();
}
void nbt_glPopMatrix(void) {
  gles2_mat_pop();
}
void nbt_glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
  gles2_mat_translate(x, y, z);
}
void nbt_glScalef(GLfloat x, GLfloat y, GLfloat z) {
  gles2_mat_scale(x, y, z);
}
void nbt_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
  gles2_mat_rotate(angle, x, y, z);
}
void nbt_glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) {
  gles2_mat_ortho((float)l, (float)r, (float)b, (float)t, (float)n, (float)f);
}

static void set_cap(GLenum cap, int enable) {
  switch (cap) {
    case GL_TEXTURE_2D:
      g_tex_enabled = enable;
      break;
    case GL_ALPHA_TEST:
      g_alpha_test = enable;
      break;
    case GL_FOG:
      g_fog_enabled = enable;
      break;
    case GL_TEXTURE_GEN_S:
      g_texgen_bits = enable ? (g_texgen_bits | 1) : (g_texgen_bits & ~1);
      break;
    case GL_TEXTURE_GEN_T:
      g_texgen_bits = enable ? (g_texgen_bits | 2) : (g_texgen_bits & ~2);
      break;
    case GL_TEXTURE_GEN_Q:
      g_texgen_bits = enable ? (g_texgen_bits | 4) : (g_texgen_bits & ~4);
      break;
    case GL_DEPTH_CLAMP_NV:
    case GL_POLYGON_SMOOTH:
    case GL_MULTISAMPLE_ARB:
    case GL_SHARED_TEXTURE_PALETTE_EXT:
      break;
    default:
      if (enable)
        glEnable(cap);
      else
        glDisable(cap);
      break;
  }
}

void nbt_glEnable(GLenum cap) {
  set_cap(cap, 1);
}
void nbt_glDisable(GLenum cap) {
  set_cap(cap, 0);
}
void nbt_glAlphaFunc(GLenum func, GLclampf ref) {
  g_alpha_func = func;
  g_alpha_ref = ref;
}
void nbt_glShadeModel(GLenum mode) {
  (void)mode;
}
void nbt_glFogi(GLenum pname, GLint param) {
  (void)pname;
  (void)param;
}
void nbt_glFogf(GLenum pname, GLfloat param) {
  if (pname == GL_FOG_DENSITY) g_fog_density = param;
}
void nbt_glFogfv(GLenum pname, const GLfloat* params) {
  if (pname == GL_FOG_COLOR && params) memcpy(g_fog_color, params, sizeof(float) * 4);
}
void nbt_glTexEnvi(GLenum target, GLenum pname, GLint param) {
  (void)target;
  if (pname == GL_TEXTURE_ENV_MODE)
    g_tex_env_base = param;
  else if (pname == GL_COMBINE_RGB)
    g_comb_rgb = param;
  else if (pname == GL_COMBINE_ALPHA)
    g_comb_alpha = param;
  else if (pname == GL_SOURCE0_RGB)
    g_src0_rgb = param;
  else if (pname == GL_OPERAND0_RGB)
    g_op0_rgb = param;
}
void nbt_glTexEnvf(GLenum target, GLenum pname, GLfloat param) {
  nbt_glTexEnvi(target, pname, (GLint)param);
}
void nbt_glTexGenf(GLenum coord, GLenum pname, GLfloat param) {
  (void)coord;
  (void)pname;
  (void)param;
}
void nbt_glTexGenfv(GLenum coord, GLenum pname, const GLfloat* params) {
  (void)coord;
  (void)pname;
  (void)params;
}

void nbt_glEnableClientState(GLenum array) {
  if (array == GL_VERTEX_ARRAY)
    g_va.enabled = 1;
  else if (array == GL_TEXTURE_COORD_ARRAY)
    g_ta.enabled = 1;
  else if (array == GL_COLOR_ARRAY)
    g_ca.enabled = 1;
}
void nbt_glDisableClientState(GLenum array) {
  if (array == GL_VERTEX_ARRAY)
    g_va.enabled = 0;
  else if (array == GL_TEXTURE_COORD_ARRAY)
    g_ta.enabled = 0;
  else if (array == GL_COLOR_ARRAY)
    g_ca.enabled = 0;
}
void nbt_glVertexPointer(GLint size, GLenum type, GLsizei stride, const void* ptr) {
  g_va.size = size;
  g_va.type = type;
  g_va.stride = stride;
  g_va.ptr = ptr;
}
void nbt_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void* ptr) {
  g_ta.size = size;
  g_ta.type = type;
  g_ta.stride = stride;
  g_ta.ptr = ptr;
}
void nbt_glColorPointer(GLint size, GLenum type, GLsizei stride, const void* ptr) {
  g_ca.size = size;
  g_ca.type = type;
  g_ca.stride = stride;
  g_ca.ptr = ptr;
}

static size_t elem_bytes(GLint size, GLenum type, GLsizei stride) {
  if (stride > 0) return (size_t)stride;
  return (size_t)size * (type == GL_UNSIGNED_BYTE ? 1u : sizeof(GLfloat));
}

static void bind_client_attr(GLuint idx, const ArrayBinding* b, GLint first, int norm,
                             const float* fallback4) {
  if (b->enabled && b->ptr) {
    const unsigned char* p =
        (const unsigned char*)b->ptr + (size_t)first * elem_bytes(b->size, b->type, b->stride);
    glEnableVertexAttribArray(idx);
    glVertexAttribPointer(idx, b->size, b->type, (GLboolean)norm, b->stride, p);
  } else {
    glDisableVertexAttribArray(idx);
    glVertexAttrib4fv(idx, fallback4);
  }
}

void nbt_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
  static const float kZeroPos[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float tc4[4];
  if (count <= 0 || !g_va.enabled || !g_va.ptr) return;
  tc4[0] = g_cur_tc[0];
  tc4[1] = g_cur_tc[1];
  tc4[2] = 0.0f;
  tc4[3] = 1.0f;
  if (!upload_uniforms()) return;
  bind_client_attr(0, &g_va, first, 0, kZeroPos);
  bind_client_attr(1, &g_ta, first, 0, tc4);
  bind_client_attr(2, &g_ca, first, 1, g_cur_color);

  if (mode == GL_QUAD_STRIP) mode = GL_TRIANGLE_STRIP;
  if (mode == GL_POLYGON) mode = GL_TRIANGLE_FAN;

  if (mode == GL_QUADS) {
    int ic = 0;
    unsigned short* idx = ensure_quad_indices(count, &ic);
    if (idx && ic > 0) glDrawElements(GL_TRIANGLES, ic, GL_UNSIGNED_SHORT, idx);
  } else {
    glDrawArrays(mode, 0, count);
  }
}

void nbt_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                      GLsizei height, GLint border, GLenum format, GLenum type, const void* data) {
  (void)internalformat;
  if (format == GL_COLOR_INDEX || (GLenum)internalformat == GL_COLOR_INDEX8_EXT) return;
  glTexImage2D(target, level, (GLint)format, width, height, border, format, type, data);
}

void nbt_glTexParameteri(GLenum target, GLenum pname, GLint param) {
  if (pname == GL_GENERATE_MIPMAP) return;
  if ((pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T) && param == GL_CLAMP) {
    param = GL_CLAMP_TO_EDGE;
  }
  glTexParameteri(target, pname, param);
}

void nbt_glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
  if (pname == GL_TEXTURE_MAX_ANISOTROPY_EXT) return;
  glTexParameterf(target, pname, param);
}

void nbt_glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void* pixels) {
  (void)target;
  (void)level;
  (void)format;
  (void)type;
  (void)pixels;
}
void nbt_glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params) {
  (void)target;
  (void)level;
  (void)pname;
  if (params) *params = 0;
}

static int is_pot(int v) {
  return v > 0 && (v & (v - 1)) == 0;
}

GLint nbt_gluBuild2DMipmaps(GLenum target, GLint internalformat, GLsizei width, GLsizei height,
                            GLenum format, GLenum type, const void* data) {
  nbt_glTexImage2D(target, 0, internalformat, width, height, 0, format, type, data);
  if (is_pot(width) && is_pot(height)) glGenerateMipmap(target);
  return 0;
}

void nbt_glClearDepth(GLclampd depth) {
  glClearDepthf((GLclampf)depth);
}
void nbt_glDepthRange(GLclampd n, GLclampd f) {
  glDepthRangef((GLclampf)n, (GLclampf)f);
}

void nbt_glGetFloatv(GLenum pname, GLfloat* params) {
  if (!params) return;
  if (pname == GL_CURRENT_COLOR) {
    memcpy(params, g_cur_color, sizeof(float) * 4);
  } else {
    glGetFloatv(pname, params);
  }
}

void nbt_glGetIntegerv(GLenum pname, GLint* params) {
  if (!params) return;
  if (pname == GL_BLEND_SRC)
    glGetIntegerv(GL_BLEND_SRC_RGB, params);
  else if (pname == GL_BLEND_DST)
    glGetIntegerv(GL_BLEND_DST_RGB, params);
  else
    glGetIntegerv(pname, params);
}

const GLubyte* nbt_glGetString(GLenum name) {
  const GLubyte* s = glGetString(name);
  return s ? s : (const GLubyte*)"";
}

GLuint nbt_glGenLists(GLsizei range) {
  (void)range;
  return 1;
}
void nbt_glNewList(GLuint list, GLenum mode) {
  (void)list;
  (void)mode;
}
void nbt_glEndList(void) {}
void nbt_glCallList(GLuint list) {
  (void)list;
}
void nbt_glDeleteLists(GLuint list, GLsizei range) {
  (void)list;
  (void)range;
}

static int g_win_w = 0, g_win_h = 0;
static int g_log_w = 0, g_log_h = 0;
static int g_vp_x = 0, g_vp_y = 0, g_vp_w = 0, g_vp_h = 0;

void gles2_set_gamma(float gamma_val) {
  g_gamma = gamma_val;
}

void gles2_set_viewport_mapping(int win_w, int win_h, int log_w, int log_h, int vp_x, int vp_y,
                                int vp_w, int vp_h) {
  g_win_w = win_w;
  g_win_h = win_h;
  g_log_w = log_w;
  g_log_h = log_h;
  g_vp_x = vp_x;
  g_vp_y = vp_y;
  g_vp_w = vp_w;
  g_vp_h = vp_h;
}

void nbt_glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
  if (g_log_w > 0 && g_log_h > 0 && g_vp_w > 0 && g_vp_h > 0) {
    GLint mx = g_vp_x + (x * g_vp_w) / g_log_w;
    GLint my = g_vp_y + (y * g_vp_h) / g_log_h;
    GLsizei mw = (w * g_vp_w) / g_log_w;
    GLsizei mh = (h * g_vp_h) / g_log_h;
    if (g_vp_w < g_win_w || g_vp_h < g_win_h) {
      glDisable(GL_SCISSOR_TEST);
      glViewport(0, 0, g_win_w, g_win_h);
    }
    glViewport(mx, my, mw, mh);
  } else {
    glViewport(x, y, w, h);
  }
}

void nbt_glScissor(GLint x, GLint y, GLsizei w, GLsizei h) {
  if (g_log_w > 0 && g_log_h > 0 && g_vp_w > 0 && g_vp_h > 0) {
    GLint mx = g_vp_x + (x * g_vp_w) / g_log_w;
    GLint my = g_vp_y + (y * g_vp_h) / g_log_h;
    GLsizei mw = (w * g_vp_w) / g_log_w;
    GLsizei mh = (h * g_vp_h) / g_log_h;
    glScissor(mx, my, mw, mh);
  } else {
    glScissor(x, y, w, h);
  }
}

void gles2_batch_shutdown(void) {
  gles2_shaders_destroy(&g_prog);
  g_prog_ready = 0;
  free(g_verts);
  g_verts = NULL;
  g_verts_cap = g_verts_count = 0;
  free(g_indices);
  g_indices = NULL;
  g_indices_cap = 0;
}

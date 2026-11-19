#ifndef NBT_GLES2_COMPAT_H
#define NBT_GLES2_COMPAT_H

#include <GLES2/gl2.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef CALLBACK
#define CALLBACK
#endif

typedef double GLdouble;
typedef double GLclampd;

/* Legacy GL1 / Desktop GL constants mapped to GLES2 compat layer */
#define GL_QUADS 0x0007
#define GL_QUAD_STRIP 0x0008
#define GL_POLYGON 0x0009

#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_TEXTURE 0x1702

#define GL_CURRENT_COLOR 0x0B00
#define GL_SHADE_MODEL 0x0B54
#define GL_FLAT 0x1D00
#define GL_SMOOTH 0x1D01

#define GL_ALPHA_TEST 0x0BC0
#define GL_FOG 0x0B60
#define GL_FOG_MODE 0x0B65
#define GL_FOG_DENSITY 0x0B62
#define GL_FOG_COLOR 0x0B66
#define GL_FOG_HINT 0x0C54
#define GL_EXP 0x0800
#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#define GL_NICEST 0x1102
#define GL_POLYGON_SMOOTH 0x0B41

#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078

#define GL_CLAMP 0x2900
#define GL_GENERATE_MIPMAP 0x8191
#define GL_TEXTURE_WIDTH 0x1000
#define GL_TEXTURE_HEIGHT 0x1001

#define GL_RGBA8 0x8058
#define GL_RGBA4 0x8056
#define GL_RGBA2 0x8055
#define GL_RGB5_A1 0x8057
#define GL_DEPTH_COMPONENT 0x1902

#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_MODULATE 0x2100
#define GL_DECAL 0x2101
#define GL_COMBINE 0x8570
#define GL_COMBINE_ARB 0x8570
#define GL_COMBINE_RGB 0x8571
#define GL_COMBINE_ALPHA 0x8572
#define GL_RGB_SCALE_ARB 0x8573
#define GL_DOT3_RGB 0x86AE
#define GL_SOURCE0_RGB 0x8580
#define GL_SOURCE1_RGB 0x8581
#define GL_SOURCE0_ALPHA 0x8588
#define GL_SOURCE1_ALPHA 0x8589
#define GL_OPERAND0_RGB 0x8590
#define GL_OPERAND1_RGB 0x8591
#define GL_OPERAND0_ALPHA 0x8598
#define GL_OPERAND1_ALPHA 0x8599
#define GL_PRIMARY_COLOR 0x8577

#define GL_TEXTURE_GEN_S 0x0C60
#define GL_TEXTURE_GEN_T 0x0C61
#define GL_TEXTURE_GEN_Q 0x0C63
#define GL_TEXTURE_GEN_MODE 0x2500
#define GL_EYE_LINEAR 0x2400
#define GL_EYE_PLANE 0x2502
#define GL_S 0x2000
#define GL_T 0x2001
#define GL_Q 0x2003

#define GL_TEXTURE0_ARB GL_TEXTURE0
#define GL_TEXTURE1_ARB GL_TEXTURE1
#define GL_TEXTURE31_ARB GL_TEXTURE31

#define GL_DEPTH_CLAMP_NV 0x864F
#define GL_MULTISAMPLE_ARB 0x809D
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_COLOR_INDEX 0x1900
#define GL_COLOR_INDEX8_EXT 0x80E5
#define GL_SHARED_TEXTURE_PALETTE_EXT 0x81FB
#define GL_CONSTANT_ALPHA_EXT 0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA_EXT 0x8004

#define GL_COMPILE 0x1300
#define GL_PACK_ALIGNMENT 0x0D05

/* Extension symbols checked by gl_opengl.c / gl_opengl.h */
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW_ARB 0x88E4
#define GL_FRAMEBUFFER_EXT 0x8D40
#define GL_RENDERBUFFER_EXT 0x8D41
#define GL_COLOR_ATTACHMENT0_EXT 0x8CE0
#define GL_DEPTH_ATTACHMENT_EXT 0x8D00
#define GL_STENCIL_ATTACHMENT_EXT 0x8D20
#define GL_FRAMEBUFFER_COMPLETE_EXT 0x8CD5
#define GL_DEPTH_STENCIL_EXT 0x84F9
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_BLEND_DST 0x0BE0
#define GL_BLEND_SRC 0x0BE1

typedef void(APIENTRY* PFNGLCOLORTABLEEXTPROC)(GLenum, GLenum, GLsizei, GLenum, GLenum,
                                               const void*);
typedef void(APIENTRY* PFNGLBINDFRAMEBUFFEREXTPROC)(GLenum, GLuint);
typedef void(APIENTRY* PFNGLGENFRAMEBUFFERSEXTPROC)(GLsizei, GLuint*);
typedef void(APIENTRY* PFNGLGENRENDERBUFFERSEXTPROC)(GLsizei, GLuint*);
typedef void(APIENTRY* PFNGLBINDRENDERBUFFEREXTPROC)(GLenum, GLuint);
typedef void(APIENTRY* PFNGLRENDERBUFFERSTORAGEEXTPROC)(GLenum, GLenum, GLsizei, GLsizei);
typedef void(APIENTRY* PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC)(GLenum, GLenum, GLenum, GLuint);
typedef void(APIENTRY* PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum(APIENTRY* PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)(GLenum);
typedef void(APIENTRY* PFNGLDELETEFRAMEBUFFERSEXTPROC)(GLsizei, const GLuint*);
typedef void(APIENTRY* PFNGLDELETERENDERBUFFERSEXTPROC)(GLsizei, const GLuint*);
typedef void(APIENTRY* PFNGLACTIVETEXTUREARBPROC)(GLenum);
typedef void(APIENTRY* PFNGLCLIENTACTIVETEXTUREARBPROC)(GLenum);
typedef void(APIENTRY* PFNGLMULTITEXCOORD2FARBPROC)(GLenum, GLfloat, GLfloat);
typedef void(APIENTRY* PFNGLMULTITEXCOORD2FVARBPROC)(GLenum, const GLfloat*);
typedef void(APIENTRY* PFNGLBLENDCOLOREXTPROC)(GLclampf, GLclampf, GLclampf, GLclampf);
typedef void(APIENTRY* PFNGLCOMPRESSEDTEXIMAGE2DARBPROC)(GLenum, GLint, GLenum, GLsizei, GLsizei,
                                                         GLint, GLsizei, const void*);
typedef void(APIENTRY* PFNGLGENBUFFERSARBPROC)(GLsizei, GLuint*);
typedef void(APIENTRY* PFNGLDELETEBUFFERSARBPROC)(GLsizei, const GLuint*);
typedef void(APIENTRY* PFNGLBINDBUFFERARBPROC)(GLenum, GLuint);
typedef void(APIENTRY* PFNGLBUFFERDATAARBPROC)(GLenum, ptrdiff_t, const void*, GLenum);
typedef void(APIENTRY* PFNGLBUFFERSUBDATAARBPROC)(GLenum, ptrdiff_t, ptrdiff_t, const void*);
typedef void(APIENTRY* PFNGLGETBUFFERPARAMETERIVARBPROC)(GLenum, GLenum, GLint*);
typedef void*(APIENTRY* PFNGLMAPBUFFERARBPROC)(GLenum, GLenum);
typedef GLboolean(APIENTRY* PFNGLUNMAPBUFFERARBPROC)(GLenum);

/* Compatibility entrypoints implemented in gles2_batch.c */
void nbt_glBegin(GLenum mode);
void nbt_glEnd(void);
void nbt_glVertex2f(GLfloat x, GLfloat y);
void nbt_glVertex2i(GLint x, GLint y);
void nbt_glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void nbt_glVertex3fv(const GLfloat* v);
void nbt_glTexCoord2f(GLfloat s, GLfloat t);
void nbt_glTexCoord2fv(const GLfloat* v);
void nbt_glColor3f(GLfloat r, GLfloat g, GLfloat b);
void nbt_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void nbt_glColor4fv(const GLfloat* v);
void nbt_glColor4ubv(const GLubyte* v);

void nbt_glMatrixMode(GLenum mode);
void nbt_glLoadIdentity(void);
void nbt_glLoadMatrixf(const GLfloat* m);
void nbt_glPushMatrix(void);
void nbt_glPopMatrix(void);
void nbt_glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void nbt_glScalef(GLfloat x, GLfloat y, GLfloat z);
void nbt_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void nbt_glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);

void nbt_glEnable(GLenum cap);
void nbt_glDisable(GLenum cap);
void nbt_glAlphaFunc(GLenum func, GLclampf ref);
void nbt_glShadeModel(GLenum mode);
void nbt_glFogi(GLenum pname, GLint param);
void nbt_glFogf(GLenum pname, GLfloat param);
void nbt_glFogfv(GLenum pname, const GLfloat* params);
void nbt_glTexEnvi(GLenum target, GLenum pname, GLint param);
void nbt_glTexEnvf(GLenum target, GLenum pname, GLfloat param);
void nbt_glTexGenf(GLenum coord, GLenum pname, GLfloat param);
void nbt_glTexGenfv(GLenum coord, GLenum pname, const GLfloat* params);

void nbt_glEnableClientState(GLenum array);
void nbt_glDisableClientState(GLenum array);
void nbt_glVertexPointer(GLint size, GLenum type, GLsizei stride, const void* ptr);
void nbt_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void* ptr);
void nbt_glColorPointer(GLint size, GLenum type, GLsizei stride, const void* ptr);
void nbt_glDrawArrays(GLenum mode, GLint first, GLsizei count);

void nbt_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                      GLsizei height, GLint border, GLenum format, GLenum type, const void* data);
void nbt_glTexParameteri(GLenum target, GLenum pname, GLint param);
void nbt_glTexParameterf(GLenum target, GLenum pname, GLfloat param);
void nbt_glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void* pixels);
void nbt_glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params);
GLint nbt_gluBuild2DMipmaps(GLenum target, GLint internalformat, GLsizei width, GLsizei height,
                            GLenum format, GLenum type, const void* data);

void nbt_glClearDepth(GLclampd depth);
void nbt_glDepthRange(GLclampd n, GLclampd f);
void nbt_glGetFloatv(GLenum pname, GLfloat* params);
void nbt_glGetIntegerv(GLenum pname, GLint* params);
const GLubyte* nbt_glGetString(GLenum name);

GLuint nbt_glGenLists(GLsizei range);
void nbt_glNewList(GLuint list, GLenum mode);
void nbt_glEndList(void);
void nbt_glCallList(GLuint list);
void nbt_glDeleteLists(GLuint list, GLsizei range);

void nbt_glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void nbt_glScissor(GLint x, GLint y, GLsizei w, GLsizei h);

void gles2_set_gamma(float gamma_val);
void gles2_set_viewport_mapping(int win_w, int win_h, int log_w, int log_h, int vp_x, int vp_y,
                                int vp_w, int vp_h);
int gles2_batch_init(void);
void gles2_batch_shutdown(void);
int gles2_expand_quad_indices(int vertex_count, unsigned short* out_indices, int max_indices);

#ifndef NBT_GLES2_IMPL
#define glBegin nbt_glBegin
#define glEnd nbt_glEnd
#define glVertex2f nbt_glVertex2f
#define glVertex2i nbt_glVertex2i
#define glVertex3f nbt_glVertex3f
#define glVertex3fv nbt_glVertex3fv
#define glTexCoord2f nbt_glTexCoord2f
#define glTexCoord2fv nbt_glTexCoord2fv
#define glColor3f nbt_glColor3f
#define glColor4f nbt_glColor4f
#define glColor4fv nbt_glColor4fv
#define glColor4ubv nbt_glColor4ubv

#define glMatrixMode nbt_glMatrixMode
#define glLoadIdentity nbt_glLoadIdentity
#define glLoadMatrixf nbt_glLoadMatrixf
#define glPushMatrix nbt_glPushMatrix
#define glPopMatrix nbt_glPopMatrix
#define glTranslatef nbt_glTranslatef
#define glScalef nbt_glScalef
#define glRotatef nbt_glRotatef
#define glOrtho nbt_glOrtho

#define glEnable nbt_glEnable
#define glDisable nbt_glDisable
#define glAlphaFunc nbt_glAlphaFunc
#define glShadeModel nbt_glShadeModel
#define glFogi nbt_glFogi
#define glFogf nbt_glFogf
#define glFogfv nbt_glFogfv
#define glTexEnvi nbt_glTexEnvi
#define glTexEnvf nbt_glTexEnvf
#define glTexGenf nbt_glTexGenf
#define glTexGenfv nbt_glTexGenfv

#define glEnableClientState nbt_glEnableClientState
#define glDisableClientState nbt_glDisableClientState
#define glVertexPointer nbt_glVertexPointer
#define glTexCoordPointer nbt_glTexCoordPointer
#define glColorPointer nbt_glColorPointer
#define glDrawArrays nbt_glDrawArrays

#define glTexImage2D nbt_glTexImage2D
#define glTexParameteri nbt_glTexParameteri
#define glTexParameterf nbt_glTexParameterf
#define glGetTexImage nbt_glGetTexImage
#define glGetTexLevelParameteriv nbt_glGetTexLevelParameteriv
#define gluBuild2DMipmaps nbt_gluBuild2DMipmaps

#define glClearDepth nbt_glClearDepth
#define glDepthRange nbt_glDepthRange
#define glGetFloatv nbt_glGetFloatv
#define glGetIntegerv nbt_glGetIntegerv
#define glGetString nbt_glGetString

#define glGenLists nbt_glGenLists
#define glNewList nbt_glNewList
#define glEndList nbt_glEndList
#define glCallList nbt_glCallList
#define glDeleteLists nbt_glDeleteLists
#define glViewport nbt_glViewport
#define glScissor nbt_glScissor
#endif

#ifdef __cplusplus
}
#endif

#endif /* NBT_GLES2_COMPAT_H */

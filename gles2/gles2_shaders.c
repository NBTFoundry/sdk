#include "gles2_shaders.h"

#include <stdio.h>
#include <string.h>

static const char* kVertexShader =
    "#version 100\n"
    "attribute vec3 a_pos;\n"
    "attribute vec2 a_tc;\n"
    "attribute vec4 a_color;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_mv;\n"
    "uniform mat4 u_texmat;\n"
    "uniform int u_texgen_enabled;\n"
    "varying vec4 v_color;\n"
    "varying vec4 v_tc;\n"
    "varying float v_eye_z;\n"
    "void main() {\n"
    "  vec4 pos4 = vec4(a_pos, 1.0);\n"
    "  vec4 eye = u_mv * pos4;\n"
    "  v_eye_z = eye.z;\n"
    "  v_color = a_color;\n"
    "  if (u_texgen_enabled != 0) {\n"
    "    v_tc = u_texmat * vec4(eye.x, eye.y, 0.0, eye.z);\n"
    "  } else {\n"
    "    v_tc = u_texmat * vec4(a_tc, 0.0, 1.0);\n"
    "  }\n"
    "  gl_Position = u_mvp * pos4;\n"
    "}\n";

static const char* kFragmentShader =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform int u_tex_enabled;\n"
    "uniform int u_tex_env_mode;\n"
    "uniform int u_alpha_test;\n"
    "uniform float u_alpha_ref;\n"
    "uniform int u_fog_enabled;\n"
    "uniform vec4 u_fog_color;\n"
    "uniform float u_fog_density;\n"
    "uniform float u_gamma;\n"
    "varying vec4 v_color;\n"
    "varying vec4 v_tc;\n"
    "varying float v_eye_z;\n"
    "void main() {\n"
    "  vec4 col = v_color;\n"
    "  if (u_tex_enabled != 0) {\n"
    "    vec2 uv = (abs(v_tc.w) > 1e-3) ? (v_tc.xy / v_tc.w) : v_tc.xy;\n"
    "    vec4 tex = texture2D(u_tex, uv);\n"
    "    if (u_tex_env_mode == 1) {\n"
    "      col = vec4(v_color.rgb, v_color.a * tex.a);\n"
    "    } else if (u_tex_env_mode == 2) {\n"
    "      col = vec4(tex.rgb * v_color.rgb, v_color.a);\n"
    "    } else if (u_tex_env_mode == 3) {\n"
    "      col = vec4((vec3(1.0) - tex.rgb) * v_color.rgb, v_color.a * tex.a);\n"
    "    } else if (u_tex_env_mode == 4) {\n"
    "      col = vec4((vec3(1.0) - tex.rgb) * v_color.rgb, v_color.a);\n"
    "    } else if (u_tex_env_mode == 5) {\n"
    "      col = vec4(mix(v_color.rgb, tex.rgb, tex.a), v_color.a);\n"
    "    } else if (u_tex_env_mode == 6) {\n"
    "      col = tex;\n"
    "    } else {\n"
    "      col = tex * v_color;\n"
    "    }\n"
    "  }\n"
    "  if (u_alpha_test == 1 && col.a < u_alpha_ref) discard;\n"
    "  if (u_alpha_test == 2 && col.a <= u_alpha_ref) discard;\n"
    "  if (u_fog_enabled != 0) {\n"
    "    float f = clamp(exp(-u_fog_density * abs(v_eye_z)), 0.0, 1.0);\n"
    "    col.rgb = mix(u_fog_color.rgb, col.rgb, f);\n"
    "  }\n"
    "  if (abs(u_gamma - 1.0) > 0.01) {\n"
    "    col.rgb = pow(max(clamp(col.rgb, 0.0, 1.0), vec3(1e-3)), vec3(1.0 / u_gamma));\n"
    "  }\n"
    "  gl_FragColor = col;\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char* src) {
  GLuint sh = glCreateShader(type);
  GLint ok = 0;
  GLint len;
  if (!sh) return 0;
  len = (GLint)strlen(src);
  glShaderSource(sh, 1, &src, &len);
  glCompileShader(sh);
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(sh, sizeof(log), NULL, log);
    fprintf(stderr, "[nbt-gles2] shader compile error: %s\n", log);
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

int gles2_shaders_init(Gles2Program* prog) {
  GLuint vs, fs, p;
  GLint ok = 0;
  if (!prog) return 0;
  memset(prog, 0, sizeof(*prog));

  vs = compile_shader(GL_VERTEX_SHADER, kVertexShader);
  fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentShader);
  if (!vs || !fs) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return 0;
  }

  p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glBindAttribLocation(p, 0, "a_pos");
  glBindAttribLocation(p, 1, "a_tc");
  glBindAttribLocation(p, 2, "a_color");
  glLinkProgram(p);
  glDeleteShader(vs);
  glDeleteShader(fs);

  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetProgramInfoLog(p, sizeof(log), NULL, log);
    fprintf(stderr, "[nbt-gles2] program link error: %s\n", log);
    glDeleteProgram(p);
    return 0;
  }

  prog->program = p;
  prog->u_mvp = glGetUniformLocation(p, "u_mvp");
  prog->u_mv = glGetUniformLocation(p, "u_mv");
  prog->u_texmat = glGetUniformLocation(p, "u_texmat");
  prog->u_tex = glGetUniformLocation(p, "u_tex");
  prog->u_tex_enabled = glGetUniformLocation(p, "u_tex_enabled");
  prog->u_texgen_enabled = glGetUniformLocation(p, "u_texgen_enabled");
  prog->u_tex_env_mode = glGetUniformLocation(p, "u_tex_env_mode");
  prog->u_alpha_test = glGetUniformLocation(p, "u_alpha_test");
  prog->u_alpha_ref = glGetUniformLocation(p, "u_alpha_ref");
  prog->u_fog_enabled = glGetUniformLocation(p, "u_fog_enabled");
  prog->u_fog_color = glGetUniformLocation(p, "u_fog_color");
  prog->u_fog_density = glGetUniformLocation(p, "u_fog_density");
  prog->u_gamma = glGetUniformLocation(p, "u_gamma");
  return 1;
}

void gles2_shaders_destroy(Gles2Program* prog) {
  if (prog && prog->program) {
    glDeleteProgram(prog->program);
    prog->program = 0;
  }
}

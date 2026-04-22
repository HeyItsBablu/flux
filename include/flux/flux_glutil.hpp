#pragma once
// ============================================================================
// flux_glutil.hpp
// Shader compilation + matrix math helpers.
//
// glad MUST be included before any GL types (GLenum, GLuint, etc.) are used.
// This header includes glad itself so callers don't have to worry about order.
// ============================================================================

// ── glad (Win32 + Linux only; Android/iOS use their own GL headers) ───────────
#if defined(_WIN32) || defined(__linux__) && !defined(__ANDROID__)
  #include <glad/glad.h>
#elif defined(__ANDROID__)
  #include <GLES3/gl3.h>
#endif



namespace glutil {

// ── Shader compilation ────────────────────────────────────────────────────────
inline GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        GLsizei len = 0;
        glGetShaderInfoLog(s, 1024, &len, buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

inline GLuint linkProgram(const char* vert, const char* frag) {
    GLuint vs = compileShader(GL_VERTEX_SHADER,   vert);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, frag);

    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }

    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        GLsizei len = 0;
        glGetProgramInfoLog(p, 1024, &len, buf);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// ── Orthographic projection ───────────────────────────────────────────────────
// Column-major, maps [l,r] x [b,t] → NDC. out must be float[16].
inline void ortho(float l, float r, float b, float t, float out[16]) {
    float rml = r - l, tmb = t - b;
    out[0]  =  2.f / rml;  out[1]  = 0;           out[2]  = 0;   out[3]  = 0;
    out[4]  =  0;          out[5]  =  2.f / tmb;  out[6]  = 0;   out[7]  = 0;
    out[8]  =  0;          out[9]  = 0;            out[10] = -1;  out[11] = 0;
    out[12] = -(r+l)/rml;  out[13] = -(t+b)/tmb;  out[14] = 0;   out[15] = 1;
}

} // namespace glutil

// ── Viewport::buildMVP ────────────────────────────────────────────────────────
// Defined here because it needs glutil::ortho, but Viewport lives in
// flux_canvas_types.hpp which must stay GL-free.
inline void Viewport::buildMVP(float out[16]) const {
    float l = offsetX_,             r = offsetX_ + vw_ / zoom_;
    float b = offsetY_,             t = offsetY_ + vh_ / zoom_;
    glutil::ortho(l, r, b, t, out);
}
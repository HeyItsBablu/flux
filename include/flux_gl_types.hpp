#ifndef FLUX_GL_TYPES_HPP
#define FLUX_GL_TYPES_HPP

// ============================================================================
// flux_gl_types.hpp
//
// All OpenGL types that the Windows <gl/GL.h> (frozen at GL 1.1) does NOT
// define.  Include this AFTER <gl/GL.h> / <windows.h> and BEFORE any GL 2.0+
// function-pointer typedefs.
//
// We define them unconditionally with a static_assert size-check so that a
// future gl.h that does define them would produce a hard error rather than a
// silent mismatch.
// ============================================================================

#include <cstddef>   // std::ptrdiff_t, std::size_t
#include <cstdint>   // std::int64_t

// ── Characters used in shader source strings ─────────────────────────────────
typedef char      GLchar;

// ── Signed / unsigned pointer-sized integers (VBO offsets and sizes) ─────────
// GLsizeiptr : signed,   large enough to hold the size of a VBO in bytes
// GLintptr   : signed,   large enough to hold a byte offset into a VBO
// The GL spec says these match the platform pointer width.
typedef std::ptrdiff_t   GLsizeiptr;
typedef std::ptrdiff_t   GLintptr;

// ── 64-bit integers (used by timer queries, NV_shader_buffer_load, etc.) ─────
// Not needed by this canvas yet, included for completeness so nothing else
// needs to reopen this header.
typedef std::int64_t     GLint64;
typedef std::uint64_t    GLuint64;

// ── Half-float (OpenGL 3.0+, OES_texture_half_float) ─────────────────────────
// Stored as a raw 16-bit unsigned int; no arithmetic operations on the CPU.
typedef unsigned short   GLhalf;

// ── Sanity checks ────────────────────────────────────────────────────────────
// Catch mismatches between our typedef and what the GL spec requires.
// All of these must be true on Win32 x86 and Win64 x64.
static_assert(sizeof(GLchar)      == 1,              "GLchar must be 1 byte");
static_assert(sizeof(GLsizeiptr)  == sizeof(void*),  "GLsizeiptr must be pointer-sized");
static_assert(sizeof(GLintptr)    == sizeof(void*),  "GLintptr must be pointer-sized");
static_assert(sizeof(GLint64)     == 8,              "GLint64 must be 8 bytes");
static_assert(sizeof(GLuint64)    == 8,              "GLuint64 must be 8 bytes");
static_assert(sizeof(GLhalf)      == 2,              "GLhalf must be 2 bytes");

#endif // FLUX_GL_TYPES_HPP
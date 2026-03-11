#pragma once

#include <glad/gl.h>
#include <cstdint>
#include <string>

// ─── Texture helpers ──────────────────────────────────────────────────────────

// Upload RGBA8 CPU data to a new GL texture; returns GLuint (caller owns).
GLuint gl_upload_texture(const uint8_t* pixels, int w, int h, int channels);

// Upload RGBA32F CPU data to a new GL texture; returns GLuint.
GLuint gl_upload_texture_f32(const float* pixels, int w, int h);

// Upload single-channel float data (for SSIM heatmap); returns GLuint.
GLuint gl_upload_texture_r32f(const float* pixels, int w, int h);

// Delete a GL texture and zero out the id.
void gl_delete_texture(GLuint& id);

// ─── ShaderProgram ────────────────────────────────────────────────────────────
struct ShaderProgram {
    GLuint id = 0;

    // Compile vert + frag source; returns false and prints errors on failure.
    bool compile(const char* vert_src, const char* frag_src);

    void use() const;
    void set_int  (const char* name, int   v)           const;
    void set_float(const char* name, float v)           const;
    void set_vec2 (const char* name, float x, float y)  const;

    bool valid() const { return id != 0; }

    ~ShaderProgram();
    ShaderProgram() = default;
    ShaderProgram(const ShaderProgram&)            = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&& o) noexcept : id(o.id) { o.id = 0; }
    ShaderProgram& operator=(ShaderProgram&& o) noexcept;
};

// ─── ScreenQuad ───────────────────────────────────────────────────────────────
// NDC quad  (-1,-1) → (1,1)  with UV (0,0) → (1,1).
// Y is NOT flipped here; callers flip in ImGui::Image uv arguments.
struct ScreenQuad {
    GLuint vao = 0;
    GLuint vbo = 0;

    void init();          // call once after GL context exists
    void draw() const;    // 2 triangles = 6 vertices

    bool valid() const { return vao != 0; }

    ~ScreenQuad();
    ScreenQuad() = default;
    ScreenQuad(const ScreenQuad&)            = delete;
    ScreenQuad& operator=(const ScreenQuad&) = delete;
};

// ─── Framebuffer Object ───────────────────────────────────────────────────────
struct FBO {
    GLuint fbo_id = 0;
    GLuint tex_id = 0;
    int    width  = 0;
    int    height = 0;

    // Create or resize; returns false on GL error.
    bool ensure(int w, int h);

    void bind()   const;
    void unbind() const;    // restores GL_FRAMEBUFFER binding to 0

    ~FBO();
    FBO() = default;
    FBO(const FBO&)            = delete;
    FBO& operator=(const FBO&) = delete;
};

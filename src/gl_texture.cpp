#include "gl_texture.h"

#include <iostream>
#include <cstring>

// ─── Texture helpers ──────────────────────────────────────────────────────────

static void set_common_params() {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

GLuint gl_upload_texture(const uint8_t* pixels, int w, int h, int channels) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    set_common_params();

    GLenum internal_fmt = GL_RGBA8;
    GLenum fmt          = GL_RGBA;
    if (channels == 1) { internal_fmt = GL_R8;   fmt = GL_RED;  }
    if (channels == 3) { internal_fmt = GL_RGB8;  fmt = GL_RGB;  }

    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal_fmt),
                 w, h, 0, fmt, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

GLuint gl_upload_texture_f32(const float* pixels, int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0,
                 GL_RGBA, GL_FLOAT, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

GLuint gl_upload_texture_r32f(const float* pixels, int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0,
                 GL_RED, GL_FLOAT, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void gl_delete_texture(GLuint& id) {
    if (id) {
        glDeleteTextures(1, &id);
        id = 0;
    }
}

// ─── ShaderProgram ────────────────────────────────────────────────────────────

static GLuint compile_stage(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[shader] compile error ("
                  << (type == GL_VERTEX_SHADER ? "vert" : "frag") << "):\n"
                  << log << "\n";
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool ShaderProgram::compile(const char* vert_src, const char* frag_src) {
    if (id) { glDeleteProgram(id); id = 0; }

    GLuint vs = compile_stage(GL_VERTEX_SHADER,   vert_src);
    GLuint fs = compile_stage(GL_FRAGMENT_SHADER, frag_src);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    id = glCreateProgram();
    glAttachShader(id, vs);
    glAttachShader(id, fs);
    glLinkProgram(id);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(id, sizeof(log), nullptr, log);
        std::cerr << "[shader] link error:\n" << log << "\n";
        glDeleteProgram(id);
        id = 0;
        return false;
    }
    return true;
}

void ShaderProgram::use() const {
    glUseProgram(id);
}

void ShaderProgram::set_int(const char* name, int v) const {
    glUniform1i(glGetUniformLocation(id, name), v);
}

void ShaderProgram::set_float(const char* name, float v) const {
    glUniform1f(glGetUniformLocation(id, name), v);
}

void ShaderProgram::set_vec2(const char* name, float x, float y) const {
    glUniform2f(glGetUniformLocation(id, name), x, y);
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& o) noexcept {
    if (this != &o) {
        if (id) glDeleteProgram(id);
        id  = o.id;
        o.id = 0;
    }
    return *this;
}

ShaderProgram::~ShaderProgram() {
    if (id) { glDeleteProgram(id); id = 0; }
}

// ─── ScreenQuad ───────────────────────────────────────────────────────────────

void ScreenQuad::init() {
    // clang-format off
    // pos (NDC)     UV
    float verts[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
    };
    // clang-format on

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    // a_pos : location 0
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(0));
    // a_uv  : location 1
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));

    glBindVertexArray(0);
}

void ScreenQuad::draw() const {
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

ScreenQuad::~ScreenQuad() {
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    if (vbo) { glDeleteBuffers(1, &vbo);      vbo = 0; }
}

// ─── FBO ─────────────────────────────────────────────────────────────────────

bool FBO::ensure(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (fbo_id && width == w && height == h) return true;

    // Destroy old
    if (fbo_id) { glDeleteFramebuffers(1, &fbo_id); fbo_id = 0; }
    if (tex_id) { glDeleteTextures(1, &tex_id);     tex_id = 0; }

    width  = w;
    height = h;

    glGenTextures(1, &tex_id);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &fbo_id);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex_id, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[FBO] incomplete framebuffer: " << status << "\n";
        glDeleteFramebuffers(1, &fbo_id); fbo_id = 0;
        glDeleteTextures(1, &tex_id);     tex_id = 0;
        return false;
    }
    return true;
}

void FBO::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_id);
    glViewport(0, 0, width, height);
}

void FBO::unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

FBO::~FBO() {
    if (fbo_id) { glDeleteFramebuffers(1, &fbo_id); }
    if (tex_id) { glDeleteTextures(1, &tex_id); }
}

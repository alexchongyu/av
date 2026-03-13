#pragma once

#include <SDL3/SDL.h>

// ─── Render backend selection ────────────────────────────────────────────────
// GL 3.3 context creation fails under X11 forwarding (XQuartz).
// Software fallback uses SDL_Renderer + imgui_impl_sdlrenderer3.

enum class RenderBackend { OpenGL, Software };

struct RenderContext {
    RenderBackend backend      = RenderBackend::OpenGL;
    SDL_Renderer* sdl_renderer = nullptr;
    SDL_GLContext gl_context    = nullptr;
};

extern RenderContext g_render_ctx;

inline bool is_software_mode() {
    return g_render_ctx.backend == RenderBackend::Software;
}

#pragma once

// GLSL #version 150  =  OpenGL 3.2 Core Profile
// Compatible with macOS (supports up to OpenGL 4.1 Core)

namespace shaders {

// ─── Shared vertex shader ─────────────────────────────────────────────────────
// Full-screen quad in NDC; passes UV (0,0)→(1,1) to fragment stage.
constexpr const char* VERTEX_SRC = R"GLSL(
#version 150

in vec2 a_pos;
in vec2 a_uv;
out vec2 v_uv;

void main() {
    v_uv        = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)GLSL";

// ─── Image display fragment shader ───────────────────────────────────────────
// Renders a single texture with pan/zoom viewport transform.
// UV origin = top-left (matches ImGui convention; Y is flipped in the
// vertex data so FBO output is already oriented correctly for ImGui::Image).
constexpr const char* IMAGE_FRAG_SRC = R"GLSL(
#version 150

uniform sampler2D u_tex;
uniform vec2  u_image_size;   // source texture dimensions (pixels)
uniform vec2  u_view_size;    // viewport dimensions (pixels)
uniform float u_zoom;         // display zoom factor
uniform vec2  u_pan;          // pan offset in image-pixels from center
uniform int   u_channel;     // 0=RGB, 1=R, 2=G, 3=B

in  vec2 v_uv;
out vec4 out_color;

void main() {
    // Map viewport UV → image UV with pan/zoom
    vec2 screen_px = v_uv * u_view_size;
    vec2 img_px    = (screen_px - u_view_size * 0.5 - u_pan) / u_zoom
                     + u_image_size * 0.5;
    vec2 uv = img_px / u_image_size;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        out_color = vec4(0.15, 0.15, 0.15, 1.0);   // out-of-bounds background
    } else {
        out_color = texture(u_tex, uv);
        if (u_channel == 1) out_color = vec4(vec3(out_color.r), out_color.a);
        else if (u_channel == 2) out_color = vec4(vec3(out_color.g), out_color.a);
        else if (u_channel == 3) out_color = vec4(vec3(out_color.b), out_color.a);

        // Pixel grid at high zoom (>= 16x)
        if (u_zoom >= 16.0) {
            vec2 frac_px = fract(img_px);
            vec2 grid_dist = min(frac_px, 1.0 - frac_px);
            float line_w = 1.0 / u_zoom;
            float grid = smoothstep(line_w, 0.0, min(grid_dist.x, grid_dist.y));
            out_color.rgb = mix(out_color.rgb, vec3(0.5), grid * 0.4);
        }
    }
}
)GLSL";

// ─── Diff fragment shader ─────────────────────────────────────────────────────
// Computes and visualizes pixel differences between two textures.
// u_diff_mode:
//   0 = PixelAbsolute  : |A - B| * amplify
//   1 = PixelRelative  : |A - B| / max(A, eps) * amplify
//   2 = FalseColor     : heatmap (blue→red)
constexpr const char* DIFF_FRAG_SRC = R"GLSL(
#version 150

uniform sampler2D u_texA;
uniform sampler2D u_texB;
uniform vec2  u_image_size;
uniform vec2  u_view_size;
uniform float u_zoom;
uniform vec2  u_pan;
uniform int   u_diff_mode;
uniform float u_amplify;

in  vec2 v_uv;
out vec4 out_color;

vec3 falsecolor(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c;
    if      (t < 0.25) c = mix(vec3(0.0, 0.0, 0.5), vec3(0.0, 0.0, 1.0), t * 4.0);
    else if (t < 0.5)  c = mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), (t - 0.25) * 4.0);
    else if (t < 0.75) c = mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), (t - 0.50) * 4.0);
    else               c = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (t - 0.75) * 4.0);
    return c;
}

void main() {
    vec2 screen_px = v_uv * u_view_size;
    vec2 img_px    = (screen_px - u_view_size * 0.5 - u_pan) / u_zoom
                     + u_image_size * 0.5;
    vec2 uv = img_px / u_image_size;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        out_color = vec4(0.1, 0.1, 0.1, 1.0);
        return;
    }

    vec4 a    = texture(u_texA, uv);
    vec4 b    = texture(u_texB, uv);
    vec4 diff = abs(a - b);

    if (u_diff_mode == 1) {    // Relative
        diff = diff / max(a + vec4(0.001), vec4(0.001));
    }

    vec3 result;
    if (u_diff_mode == 2) {    // FalseColor
        float intensity = (diff.r + diff.g + diff.b) / 3.0;
        result = falsecolor(intensity * u_amplify);
    } else {                   // Absolute or Relative → direct
        result = clamp(diff.rgb * u_amplify, 0.0, 1.0);
    }

    out_color = vec4(result, 1.0);

    // Pixel grid at high zoom (>= 16x)
    if (u_zoom >= 16.0) {
        vec2 frac_px = fract(img_px);
        vec2 grid_dist = min(frac_px, 1.0 - frac_px);
        float line_w = 1.0 / u_zoom;
        float grid = smoothstep(line_w, 0.0, min(grid_dist.x, grid_dist.y));
        out_color.rgb = mix(out_color.rgb, vec3(0.5), grid * 0.4);
    }
}
)GLSL";

// ─── SSIM heatmap display shader ─────────────────────────────────────────────
// Displays a precomputed SSIM heatmap texture (single-channel float).
constexpr const char* SSIM_FRAG_SRC = R"GLSL(
#version 150

uniform sampler2D u_tex;      // single-channel float SSIM map (0=same,1=diff)
uniform vec2  u_image_size;
uniform vec2  u_view_size;
uniform float u_zoom;
uniform vec2  u_pan;
uniform float u_amplify;

in  vec2 v_uv;
out vec4 out_color;

vec3 falsecolor(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c;
    if      (t < 0.25) c = mix(vec3(0.0, 0.0, 0.5), vec3(0.0, 0.0, 1.0), t * 4.0);
    else if (t < 0.5)  c = mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), (t - 0.25) * 4.0);
    else if (t < 0.75) c = mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), (t - 0.50) * 4.0);
    else               c = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (t - 0.75) * 4.0);
    return c;
}

void main() {
    vec2 screen_px = v_uv * u_view_size;
    vec2 img_px    = (screen_px - u_view_size * 0.5 - u_pan) / u_zoom
                     + u_image_size * 0.5;
    vec2 uv = img_px / u_image_size;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        out_color = vec4(0.1, 0.1, 0.1, 1.0);
        return;
    }

    float v = texture(u_tex, uv).r * u_amplify;
    out_color = vec4(falsecolor(v), 1.0);
}
)GLSL";

} // namespace shaders

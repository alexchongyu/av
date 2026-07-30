#include "image_panel.h"
#include "../shader_sources.h"
#include "../viewport.h"
#include "../render_backend.h"
#include "../image_save.h"
#include "../clipboard_image.h"

#include <imgui.h>
#include <glad/gl.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <cstring>

// ─── Pixel format helper ──────────────────────────────────────────────────────

static void fmt_int_pixel(char* buf, size_t sz, int val, PixelFormat fmt) {
    switch (fmt) {
    case PixelFormat::Hex0x: std::snprintf(buf, sz, "0x%02X", val); break;
    case PixelFormat::HexH:  std::snprintf(buf, sz, "%02Xh", val);  break;
    default:                 std::snprintf(buf, sz, "%d", val);     break;
    }
}

// ─── init ─────────────────────────────────────────────────────────────────────

bool ImagePanel::init() {
    if (is_software_mode()) {
        // No shaders needed in software mode
        inited_ = true;
        return true;
    }

    if (!image_shader_.compile(shaders::VERTEX_SRC, shaders::IMAGE_FRAG_SRC)) {
        std::cerr << "[ImagePanel] image shader compile failed\n";
        return false;
    }
    // Bind attribute locations for #version 150
    glBindAttribLocation(image_shader_.id, 0, "a_pos");
    glBindAttribLocation(image_shader_.id, 1, "a_uv");
    glLinkProgram(image_shader_.id);

    if (!blend_shader_.compile(shaders::VERTEX_SRC, shaders::BLEND_FRAG_SRC)) {
        std::cerr << "[ImagePanel] blend shader compile failed\n";
        return false;
    }
    glBindAttribLocation(blend_shader_.id, 0, "a_pos");
    glBindAttribLocation(blend_shader_.id, 1, "a_uv");
    glLinkProgram(blend_shader_.id);

    quad_.init();
    inited_ = true;
    return true;
}

// Forward declaration. `active` = 활성 패널 강조 (더 두꺼운 선 + 밝은 색).
static void draw_image_border(ImDrawList* dl, ImVec2 widget_pos,
                               float view_w, float view_h,
                               float img_w,  float img_h,
                               float pan_x,  float pan_y, float zoom,
                               ImU32 border_col,
                               bool  active = false);

// ─── Software renderer helpers ─────────────────────────────────────────────────

SDL_Texture* ImagePanel::ensure_soft_texture(int w, int h) {
    if (soft_texture_ && soft_w_ == w && soft_h_ == h) return soft_texture_;
    if (soft_texture_) SDL_DestroyTexture(soft_texture_);

    soft_texture_ = SDL_CreateTexture(g_render_ctx.sdl_renderer,
                                       SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       w, h);
    soft_w_ = w;
    soft_h_ = h;
    soft_buf_.resize(static_cast<size_t>(w) * h * 4);
    return soft_texture_;
}

// ─── falsecolor: shared by diff + SSIM ──────────────────────────────────────
static void falsecolor(float t, uint8_t* rgb) {
    t = std::clamp(t, 0.0f, 1.0f);
    float r, g, b;
    if      (t < 0.25f) { float s = t * 4.0f;        r = 0;            g = 0;            b = 0.5f + 0.5f * s; }
    else if (t < 0.50f) { float s = (t - 0.25f) * 4; r = 0;            g = s;            b = 1.0f - s; }
    else if (t < 0.75f) { float s = (t - 0.50f) * 4; r = s;            g = 1.0f;         b = 0; }
    else                { float s = (t - 0.75f) * 4; r = 1.0f;         g = 1.0f - s;     b = 0; }
    rgb[0] = static_cast<uint8_t>(r * 255.0f);
    rgb[1] = static_cast<uint8_t>(g * 255.0f);
    rgb[2] = static_cast<uint8_t>(b * 255.0f);
}

// ─── apply channel filter + pixel grid to a rendered pixel ───────────────────
static inline void apply_channel_grid(uint8_t* dst, float img_fx, float img_fy,
                                       float zoom, ChannelMode channel) {
    // Channel filter (same as shader u_channel logic)
    if (channel == ChannelMode::Red)   { dst[1] = dst[0]; dst[2] = dst[0]; }
    if (channel == ChannelMode::Green) { dst[0] = dst[1]; dst[2] = dst[1]; }
    if (channel == ChannelMode::Blue)  { dst[0] = dst[2]; dst[1] = dst[2]; }
    if (channel == ChannelMode::Luma)  {
        uint8_t y = static_cast<uint8_t>(0.2126f*dst[0] + 0.7152f*dst[1] + 0.0722f*dst[2] + 0.5f);
        dst[0] = dst[1] = dst[2] = y;
    }

    // Pixel grid at high zoom (>= 16x) — matches shader smoothstep logic
    if (zoom >= 16.0f) {
        float frac_x = img_fx - std::floor(img_fx);
        float frac_y = img_fy - std::floor(img_fy);
        float gx = std::min(frac_x, 1.0f - frac_x);
        float gy = std::min(frac_y, 1.0f - frac_y);
        float grid_dist = std::min(gx, gy);
        float line_w = 1.0f / zoom;
        // smoothstep(line_w, 0, grid_dist)
        float grid = (grid_dist >= line_w) ? 0.0f :
                     (grid_dist <= 0.0f)   ? 1.0f :
                     1.0f - grid_dist / line_w;  // linear approx of smoothstep
        grid = grid * grid * (3.0f - 2.0f * grid); // actual smoothstep
        float mix_a = grid * 0.4f;
        for (int c = 0; c < 3; ++c) {
            float v = dst[c] / 255.0f;
            v = v * (1.0f - mix_a) + 0.5f * mix_a;
            dst[c] = static_cast<uint8_t>(std::clamp(v * 255.0f, 0.0f, 255.0f));
        }
    }
}

void ImagePanel::cpu_render_image(const ImageEntry& img, const ViewportState& vp,
                                   uint8_t* buf, int view_w, int view_h,
                                   ChannelMode channel) {
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img.width * 0.5f;
    float half_ih = img.height * 0.5f;

    const uint8_t* src = img.pixels.data();
    int iw = img.width, ih = img.height;

    for (int sy = 0; sy < view_h; ++sy) {
        for (int sx = 0; sx < view_w; ++sx) {
            float img_fx = (sx - half_vw) / vp.zoom - vp.pan_x + half_iw;
            float img_fy = (sy - half_vh) / vp.zoom - vp.pan_y + half_ih;

            uint8_t* dst = buf + (sy * view_w + sx) * 4;

            if (img_fx < 0.0f || img_fy < 0.0f ||
                img_fx >= iw || img_fy >= ih) {
                dst[0] = 38; dst[1] = 38; dst[2] = 38; dst[3] = 255;
                continue;
            }

            if (vp.zoom >= 1.0f) {
                int ix = static_cast<int>(img_fx);
                int iy = static_cast<int>(img_fy);
                ix = std::clamp(ix, 0, iw - 1);
                iy = std::clamp(iy, 0, ih - 1);
                const uint8_t* p = src + (iy * iw + ix) * 4;
                dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2]; dst[3] = p[3];
            } else {
                float fx = img_fx - 0.5f;
                float fy = img_fy - 0.5f;
                int x0 = static_cast<int>(std::floor(fx));
                int y0 = static_cast<int>(std::floor(fy));
                int x1 = x0 + 1, y1 = y0 + 1;
                float tx = fx - x0, ty = fy - y0;
                x0 = std::clamp(x0, 0, iw - 1); x1 = std::clamp(x1, 0, iw - 1);
                y0 = std::clamp(y0, 0, ih - 1); y1 = std::clamp(y1, 0, ih - 1);
                const uint8_t* p00 = src + (y0 * iw + x0) * 4;
                const uint8_t* p10 = src + (y0 * iw + x1) * 4;
                const uint8_t* p01 = src + (y1 * iw + x0) * 4;
                const uint8_t* p11 = src + (y1 * iw + x1) * 4;
                for (int c = 0; c < 4; ++c) {
                    float top = p00[c] * (1 - tx) + p10[c] * tx;
                    float bot = p01[c] * (1 - tx) + p11[c] * tx;
                    dst[c] = static_cast<uint8_t>(std::clamp(top * (1 - ty) + bot * ty, 0.0f, 255.0f));
                }
            }

            apply_channel_grid(dst, img_fx, img_fy, vp.zoom, channel);
        }
    }
}

// 빈 패널 표시: --pair 모드에서 매칭 영상이 없으면 안내 문구, 아니면 기존 "(no image)".
// actual_idx = swap 반영된 데이터 슬롯 인덱스.
static void draw_empty_panel_msg(const AppState& state, int actual_idx) {
    const std::string& miss = state.panel_missing_msg[actual_idx];
    if (state.pair_mode && !miss.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "No matching image:");
        ImGui::TextDisabled("%s", miss.c_str());
        if (!state.pair_dir_b.empty())
            ImGui::TextDisabled("in %s", state.pair_dir_b.c_str());
    } else {
        ImGui::TextDisabled("(no image)");
    }
}

void ImagePanel::render_single_software(AppState& state, int panel_idx) {
    int actual_idx = state.swap_images ? (1 - panel_idx) : panel_idx;
    const ImageEntry& img = state.images[actual_idx];
    ViewportState&    vp  = state.views[panel_idx];

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int    pw    = std::max(1, static_cast<int>(avail.x));
    int    ph    = std::max(1, static_cast<int>(avail.y));

    if (!img.loaded || img.texture_id == 0 || img.pixels.empty()) {
        draw_empty_panel_msg(state, actual_idx);
        return;
    }

    if (vp.fit) {
        viewport_fit(vp, img.width, img.height, pw, ph);
    }
    viewport_clamp_pan(vp, img.width, img.height, pw, ph);

    // CPU render image into buffer
    SDL_Texture* tex = ensure_soft_texture(pw, ph);
    if (!tex) return;

    cpu_render_image(img, vp, soft_buf_.data(), pw, ph, state.channel_mode);

    // 3D LUT / CDL display transform (software): per output pixel; matches GPU gate.
    if (state.lut_enabled && state.lut_grid.loaded &&
        state.diff.mode != DiffState::Mode::SSIM &&
        state.diff.mode != DiffState::Mode::FLIP) {
        for (int i = 0; i < pw * ph; ++i) {
            uint8_t* p = &soft_buf_[i*4];
            float r = p[0]/255.0f, g = p[1]/255.0f, b = p[2]/255.0f;
            lut_apply(state.lut_grid, r, g, b);
            p[0] = (uint8_t)(std::clamp(r,0.0f,1.0f)*255.0f + 0.5f);
            p[1] = (uint8_t)(std::clamp(g,0.0f,1.0f)*255.0f + 0.5f);
            p[2] = (uint8_t)(std::clamp(b,0.0f,1.0f)*255.0f + 0.5f);
        }
    }

    // Upload to SDL texture
    SDL_UpdateTexture(tex, nullptr, soft_buf_.data(), pw * 4);

    // Display via ImGui
    ImTextureID imgui_tex = reinterpret_cast<ImTextureID>(tex);
    ImGui::Image(imgui_tex, avail, ImVec2(0, 0), ImVec2(1, 1));
    bool img_hovered = ImGui::IsItemHovered();

    ImVec2 widget_pos = ImGui::GetItemRectMin();
    if (state.roi.active) {
        handle_roi_drag(state, panel_idx, widget_pos, pw, ph,
                        img.width, img.height);
    } else {
        handle_mouse_pan(state, panel_idx);
        handle_mouse_double_click(state, panel_idx, widget_pos, pw, ph,
                                   img.width, img.height);
        handle_mouse_right_select(state, panel_idx, widget_pos, pw, ph,
                                   img.width, img.height, panel_idx);
    }

    if (state.show_borders)
        draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                          static_cast<float>(pw), static_cast<float>(ph),
                          static_cast<float>(img.width), static_cast<float>(img.height),
                          vp.pan_x, vp.pan_y, vp.zoom,
                          static_cast<ImU32>(state.border_colors[panel_idx]),
                          /*active=*/ panel_idx == state.active_panel);

    if (state.roi.has_roi || state.roi.dragging) {
        render_roi_overlay(state, panel_idx, widget_pos, pw, ph,
                           img.width, img.height);
    }

    if (state.comp.active && state.comp.computed) {
        render_comp_overlay(state, panel_idx, widget_pos, pw, ph,
                            img.width, img.height);
    }

    if (vp.zoom >= 32.0f && img.loaded) {
        render_pixel_values(state, panel_idx, widget_pos, pw, ph);
    }

    update_mouse_constraint(state, panel_idx, widget_pos, pw, ph,
                            img.width, img.height, img_hovered);
    render_magnifier(state, panel_idx, widget_pos, pw, ph,
                     img.width, img.height, false, img_hovered);
    render_crosshair(state, panel_idx, widget_pos, pw, ph, img.width, img.height);
    render_bad_pixel_overlay(state, panel_idx, widget_pos, pw, ph, img.width, img.height);

    if (panel_idx == 0) {
        render_pathfinder(state, panel_idx, widget_pos, pw, ph);
    }
}

// ─── cpu_render_diff ──────────────────────────────────────────────────────────
// CPU version of DIFF_FRAG_SRC shader logic.

static inline void sample_pixel(const ImageEntry& img, float img_fx, float img_fy,
                                 float zoom, float out[4]) {
    int iw = img.width, ih = img.height;
    if (img_fx < 0 || img_fy < 0 || img_fx >= iw || img_fy >= ih) {
        out[0] = out[1] = out[2] = 0; out[3] = 1; return;
    }

    // float32 데이터 우선 (HDR 또는 고정밀 PPM)
    if (!img.pixels_f32.empty()) {
        const float* src = img.pixels_f32.data();
        if (zoom >= 1.0f) {
            int ix = std::clamp((int)img_fx, 0, iw - 1);
            int iy = std::clamp((int)img_fy, 0, ih - 1);
            const float* p = src + (iy * iw + ix) * 4;
            for (int c = 0; c < 4; ++c) out[c] = p[c];
        } else {
            float fx = img_fx - 0.5f, fy = img_fy - 0.5f;
            int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
            int x1 = x0 + 1, y1 = y0 + 1;
            float tx = fx - x0, ty = fy - y0;
            x0 = std::clamp(x0, 0, iw-1); x1 = std::clamp(x1, 0, iw-1);
            y0 = std::clamp(y0, 0, ih-1); y1 = std::clamp(y1, 0, ih-1);
            const float* p00 = src + (y0*iw+x0)*4;
            const float* p10 = src + (y0*iw+x1)*4;
            const float* p01 = src + (y1*iw+x0)*4;
            const float* p11 = src + (y1*iw+x1)*4;
            for (int c = 0; c < 4; ++c) {
                float top = p00[c] * (1-tx) + p10[c] * tx;
                float bot = p01[c] * (1-tx) + p11[c] * tx;
                out[c] = top * (1-ty) + bot * ty;
            }
        }
        return;
    }

    // 기존 8-bit 경로
    const uint8_t* src = img.pixels.data();
    if (zoom >= 1.0f) {
        int ix = std::clamp((int)img_fx, 0, iw - 1);
        int iy = std::clamp((int)img_fy, 0, ih - 1);
        const uint8_t* p = src + (iy * iw + ix) * 4;
        for (int c = 0; c < 4; ++c) out[c] = p[c] / 255.0f;
    } else {
        float fx = img_fx - 0.5f, fy = img_fy - 0.5f;
        int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        int x1 = x0 + 1, y1 = y0 + 1;
        float tx = fx - x0, ty = fy - y0;
        x0 = std::clamp(x0, 0, iw-1); x1 = std::clamp(x1, 0, iw-1);
        y0 = std::clamp(y0, 0, ih-1); y1 = std::clamp(y1, 0, ih-1);
        const uint8_t* p00 = src + (y0*iw+x0)*4;
        const uint8_t* p10 = src + (y0*iw+x1)*4;
        const uint8_t* p01 = src + (y1*iw+x0)*4;
        const uint8_t* p11 = src + (y1*iw+x1)*4;
        for (int c = 0; c < 4; ++c) {
            float top = p00[c]/255.0f * (1-tx) + p10[c]/255.0f * tx;
            float bot = p01[c]/255.0f * (1-tx) + p11[c]/255.0f * tx;
            out[c] = top * (1-ty) + bot * ty;
        }
    }
}

void ImagePanel::cpu_render_diff(const ImageEntry& imgA, const ImageEntry& imgB,
                                  const ViewportState& vp, uint8_t* buf,
                                  int view_w, int view_h,
                                  DiffState::Mode mode, float amplify,
                                  ChannelMode channel,
                                  float enh_min, float enh_max,
                                  float alpha) {
    float half_vw = view_w * 0.5f, half_vh = view_h * 0.5f;
    float half_iw = imgA.width * 0.5f, half_ih = imgA.height * 0.5f;

    for (int sy = 0; sy < view_h; ++sy) {
        for (int sx = 0; sx < view_w; ++sx) {
            float img_fx = (sx - half_vw) / vp.zoom - vp.pan_x + half_iw;
            float img_fy = (sy - half_vh) / vp.zoom - vp.pan_y + half_ih;
            uint8_t* dst = buf + (sy * view_w + sx) * 4;

            if (img_fx < 0 || img_fy < 0 || img_fx >= imgA.width || img_fy >= imgA.height) {
                dst[0] = 26; dst[1] = 26; dst[2] = 26; dst[3] = 255;
                continue;
            }

            float a[4], b_val[4];
            sample_pixel(imgA, img_fx, img_fy, vp.zoom, a);
            sample_pixel(imgB, img_fx, img_fy, vp.zoom, b_val);

            // AlphaBlend: diff 계산을 건너뛰고 단순 선형 혼합
            if (mode == DiffState::Mode::AlphaBlend) {
                float blend[3];
                for (int c = 0; c < 3; ++c)
                    blend[c] = std::clamp((1.0f - alpha) * a[c] + alpha * b_val[c], 0.0f, 1.0f);
                if      (channel == ChannelMode::Red)   { dst[0]=dst[1]=dst[2] = (uint8_t)(blend[0]*255); }
                else if (channel == ChannelMode::Green) { dst[0]=dst[1]=dst[2] = (uint8_t)(blend[1]*255); }
                else if (channel == ChannelMode::Blue)  { dst[0]=dst[1]=dst[2] = (uint8_t)(blend[2]*255); }
                else { dst[0]=(uint8_t)(blend[0]*255); dst[1]=(uint8_t)(blend[1]*255); dst[2]=(uint8_t)(blend[2]*255); }
                dst[3] = 255;
            } else {

            float diff[3] = { std::fabs(a[0]-b_val[0]), std::fabs(a[1]-b_val[1]), std::fabs(a[2]-b_val[2]) };

            if (mode == DiffState::Mode::PixelRelative) {
                constexpr float eps = 0.001f;
                for (int c = 0; c < 3; ++c)
                    diff[c] = diff[c] / std::max(a[c] + eps, eps);
            }

            float r, g, b_out;
            if (mode == DiffState::Mode::Highlight) {
                float max_d;
                if      (channel == ChannelMode::Red)   max_d = diff[0];
                else if (channel == ChannelMode::Green) max_d = diff[1];
                else if (channel == ChannelMode::Blue)  max_d = diff[2];
                else    max_d = std::max({diff[0], diff[1], diff[2]});
                max_d *= amplify;
                if (max_d > 0.0f) {
                    float bright = 0.502f + std::clamp(max_d, 0.0f, 1.0f) * 0.498f;
                    dst[0] = (uint8_t)(bright * 255); dst[1] = 0; dst[2] = 0; dst[3] = 255;
                } else {
                    dst[0] = 0; dst[1] = 0; dst[2] = 0; dst[3] = 255;
                }
            } else if (mode == DiffState::Mode::FalseColor) {
                float intensity;
                if      (channel == ChannelMode::Red)   intensity = diff[0];
                else if (channel == ChannelMode::Green) intensity = diff[1];
                else if (channel == ChannelMode::Blue)  intensity = diff[2];
                else    intensity = (diff[0] + diff[1] + diff[2]) / 3.0f;
                uint8_t fc[3];
                falsecolor(intensity * amplify, fc);
                dst[0] = fc[0]; dst[1] = fc[1]; dst[2] = fc[2]; dst[3] = 255;
            } else if (mode == DiffState::Mode::Enhance) {
                int ch_idx = (channel == ChannelMode::Red) ? 0 :
                             (channel == ChannelMode::Green) ? 1 :
                             (channel == ChannelMode::Blue) ? 2 : -1;
                bool zero_check = (ch_idx >= 0) ? (diff[ch_idx] == 0.0f)
                                 : (diff[0] == 0.0f && diff[1] == 0.0f && diff[2] == 0.0f);
                if (zero_check) {
                    dst[0] = dst[1] = dst[2] = 0; dst[3] = 255;
                } else {
                    float range = (enh_max > enh_min) ? (enh_max - enh_min) : (1.0f/255.0f);
                    if (ch_idx >= 0) {
                        float t = std::clamp((diff[ch_idx] - enh_min) / range, 0.0f, 1.0f);
                        uint8_t v = (diff[ch_idx] > 0.0f) ? static_cast<uint8_t>(128.0f + t * 127.0f) : 0;
                        dst[0] = dst[1] = dst[2] = v; dst[3] = 255;
                    } else {
                        for (int c = 0; c < 3; ++c) {
                            if (diff[c] == 0.0f) { dst[c] = 0; }
                            else {
                                float t = std::clamp((diff[c] - enh_min) / range, 0.0f, 1.0f);
                                dst[c] = static_cast<uint8_t>(128.0f + t * 127.0f);
                            }
                        }
                        dst[3] = 255;
                    }
                }
            } else if (mode == DiffState::Mode::Signed) {
                // bwr diverging on luma(A-B): blue=A<B, white=equal, red=A>B
                float sl = (0.2126f*(a[0]-b_val[0]) + 0.7152f*(a[1]-b_val[1]) + 0.0722f*(a[2]-b_val[2])) * amplify;
                float t = std::clamp(sl, -1.0f, 1.0f);
                if (t >= 0.0f) { r = 1.0f;      g = 1.0f - t; b_out = 1.0f - t; }
                else           { r = 1.0f + t;  g = 1.0f + t; b_out = 1.0f;     }
                dst[0]=(uint8_t)(std::clamp(r,0.0f,1.0f)*255); dst[1]=(uint8_t)(std::clamp(g,0.0f,1.0f)*255);
                dst[2]=(uint8_t)(std::clamp(b_out,0.0f,1.0f)*255); dst[3]=255;
            } else {
                float d[3] = { diff[0]*amplify, diff[1]*amplify, diff[2]*amplify };
                if      (channel == ChannelMode::Red)   { r = g = b_out = std::clamp(d[0], 0.0f, 1.0f); }
                else if (channel == ChannelMode::Green) { r = g = b_out = std::clamp(d[1], 0.0f, 1.0f); }
                else if (channel == ChannelMode::Blue)  { r = g = b_out = std::clamp(d[2], 0.0f, 1.0f); }
                else { r = std::clamp(d[0], 0.0f, 1.0f); g = std::clamp(d[1], 0.0f, 1.0f); b_out = std::clamp(d[2], 0.0f, 1.0f); }
                dst[0] = (uint8_t)(r*255); dst[1] = (uint8_t)(g*255); dst[2] = (uint8_t)(b_out*255); dst[3] = 255;
            }
            } // end of else (not AlphaBlend)

            // Grid overlay (no channel filter needed — already applied in diff logic)
            if (vp.zoom >= 16.0f) {
                float frac_x = img_fx - std::floor(img_fx);
                float frac_y = img_fy - std::floor(img_fy);
                float gx = std::min(frac_x, 1.0f - frac_x);
                float gy = std::min(frac_y, 1.0f - frac_y);
                float grid_dist = std::min(gx, gy);
                float line_w = 1.0f / vp.zoom;
                float grid = (grid_dist >= line_w) ? 0.0f :
                             (grid_dist <= 0.0f)   ? 1.0f :
                             1.0f - grid_dist / line_w;
                grid = grid * grid * (3.0f - 2.0f * grid);
                float mix_a = grid * 0.4f;
                for (int c = 0; c < 3; ++c) {
                    float v = dst[c] / 255.0f;
                    v = v * (1.0f - mix_a) + 0.5f * mix_a;
                    dst[c] = (uint8_t)std::clamp(v * 255.0f, 0.0f, 255.0f);
                }
            }
        }
    }
}

// ─── compute_diff_pixel_list ─────────────────────────────────────────────────
// Compute diff pixel listing for DiffListingState (image-space, not viewport)

static void compute_diff_pixel_list(
    const ImageEntry& imgA, const ImageEntry& imgB,
    DiffListingState& listing)
{
    // 캐시 체크
    if (listing.computed &&
        listing.cache_img_a_w == imgA.width && listing.cache_img_a_h == imgA.height &&
        listing.cache_img_b_w == imgB.width && listing.cache_img_b_h == imgB.height &&
        listing.cache_ver_a == imgA.content_version &&
        listing.cache_ver_b == imgB.content_version) {
        return;
    }

    listing.pixels.clear();
    listing.identical = true;
    listing.identical_r = true;
    listing.identical_g = true;
    listing.identical_b = true;
    listing.computed  = true;
    listing.cache_img_a_w = imgA.width;  listing.cache_img_a_h = imgA.height;
    listing.cache_img_b_w = imgB.width;  listing.cache_img_b_h = imgB.height;
    listing.cache_ver_a = imgA.content_version;
    listing.cache_ver_b = imgB.content_version;

    int w = std::min(imgA.width, imgB.width);
    int h = std::min(imgA.height, imgB.height);

    // High-bit-depth PPM: pixels_orig (uint16_t, RGB 3ch)
    bool has_orig = imgA.ppm_maxval > 0 && !imgA.pixels_orig.empty()
                 && imgB.ppm_maxval > 0 && !imgB.pixels_orig.empty();
    // HDR float: pixels_f32 (float, RGBA 4ch)
    bool has_f32 = !imgA.pixels_f32.empty() && !imgB.pixels_f32.empty();

    if (has_orig) {
        const uint16_t* pA = imgA.pixels_orig.data();
        const uint16_t* pB = imgB.pixels_orig.data();
        if (!pA || !pB) return;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int offA = (y * imgA.width + x) * 3;
                int offB = (y * imgB.width + x) * 3;
                int dr = std::abs((int)pA[offA+0] - (int)pB[offB+0]);
                int dg = std::abs((int)pA[offA+1] - (int)pB[offB+1]);
                int db = std::abs((int)pA[offA+2] - (int)pB[offB+2]);
                if (dr != 0 || dg != 0 || db != 0) {
                    listing.identical = false;
                    if (dr != 0) listing.identical_r = false;
                    if (dg != 0) listing.identical_g = false;
                    if (db != 0) listing.identical_b = false;
                    listing.pixels.push_back({
                        x, y,
                        (int)pA[offA+0], (int)pA[offA+1], (int)pA[offA+2],
                        (int)pB[offB+0], (int)pB[offB+1], (int)pB[offB+2],
                        dr, dg, db
                    });
                }
            }
        }
    } else if (has_f32) {
        const float* pA = imgA.pixels_f32.data();
        const float* pB = imgB.pixels_f32.data();
        if (!pA || !pB) return;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int offA = (y * imgA.width + x) * 4;
                int offB = (y * imgB.width + x) * 4;
                int ar = (int)std::clamp(pA[offA+0] * 255.0f, 0.0f, 255.0f);
                int ag = (int)std::clamp(pA[offA+1] * 255.0f, 0.0f, 255.0f);
                int ab = (int)std::clamp(pA[offA+2] * 255.0f, 0.0f, 255.0f);
                int br = (int)std::clamp(pB[offB+0] * 255.0f, 0.0f, 255.0f);
                int bg = (int)std::clamp(pB[offB+1] * 255.0f, 0.0f, 255.0f);
                int bb = (int)std::clamp(pB[offB+2] * 255.0f, 0.0f, 255.0f);
                int dr = std::abs(ar - br);
                int dg = std::abs(ag - bg);
                int db = std::abs(ab - bb);
                if (dr != 0 || dg != 0 || db != 0) {
                    listing.identical = false;
                    if (dr != 0) listing.identical_r = false;
                    if (dg != 0) listing.identical_g = false;
                    if (db != 0) listing.identical_b = false;
                    listing.pixels.push_back({
                        x, y, ar, ag, ab, br, bg, bb, dr, dg, db
                    });
                }
            }
        }
    } else {
        const uint8_t* pA = imgA.pixels.data();
        const uint8_t* pB = imgB.pixels.data();
        if (!pA || !pB) return;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int offA = (y * imgA.width + x) * 4;
                int offB = (y * imgB.width + x) * 4;
                int dr = std::abs((int)pA[offA+0] - (int)pB[offB+0]);
                int dg = std::abs((int)pA[offA+1] - (int)pB[offB+1]);
                int db = std::abs((int)pA[offA+2] - (int)pB[offB+2]);
                if (dr != 0 || dg != 0 || db != 0) {
                    listing.identical = false;
                    if (dr != 0) listing.identical_r = false;
                    if (dg != 0) listing.identical_g = false;
                    if (db != 0) listing.identical_b = false;
                    listing.pixels.push_back({
                        x, y,
                        pA[offA+0], pA[offA+1], pA[offA+2],
                        pB[offB+0], pB[offB+1], pB[offB+2],
                        dr, dg, db
                    });
                }
            }
        }
    }
}

// ─── compute_enhance_range ────────────────────────────────────────────────────
// Compute global min/max of non-zero diff values for Enhance mode remap.

static void compute_enhance_range(const DiffListingState& listing, DiffState& diff) {
    if (diff.enhance_range_computed) return;
    diff.enhance_range_computed = true;
    if (listing.pixels.empty()) { diff.enhance_min = diff.enhance_max = 0; return; }
    int gmin = INT_MAX, gmax = 0;
    for (const auto& p : listing.pixels) {
        for (int d : {p.dr, p.dg, p.db}) {
            if (d > 0) { gmin = std::min(gmin, d); gmax = std::max(gmax, d); }
        }
    }
    diff.enhance_min = (gmax > 0) ? gmin : 0;
    diff.enhance_max = gmax;
}

// ─── count_nonzero_diff_pixels ───────────────────────────────────────────────
// Count non-zero diff pixels for Highlight mode status bar display.
// Returns (nonzero_count, total_count). Does NOT modify the buffer.

static std::pair<int,int> count_nonzero_diff_pixels(
    const ImageEntry& imgA, const ImageEntry& imgB,
    int view_w, int view_h,
    const ViewportState& vp)
{
    float half_vw = view_w * 0.5f, half_vh = view_h * 0.5f;
    float half_iw = imgA.width * 0.5f, half_ih = imgA.height * 0.5f;
    int nonzero = 0, total = 0;

    for (int sy = 0; sy < view_h; ++sy) {
        for (int sx = 0; sx < view_w; ++sx) {
            float img_fx = (sx - half_vw) / vp.zoom - vp.pan_x + half_iw;
            float img_fy = (sy - half_vh) / vp.zoom - vp.pan_y + half_ih;

            if (img_fx < 0 || img_fy < 0 || img_fx >= imgA.width || img_fy >= imgA.height)
                continue;

            float a[4], b_val[4];
            sample_pixel(imgA, img_fx, img_fy, vp.zoom, a);
            sample_pixel(imgB, img_fx, img_fy, vp.zoom, b_val);
            float max_diff = 0.0f;
            for (int c = 0; c < 3; ++c) {
                float d = std::fabs(a[c] - b_val[c]);
                if (d > max_diff) max_diff = d;
            }

            ++total;
            if (max_diff > 0.0f) ++nonzero;
        }
    }
    return {nonzero, total};
}

// ─── apply_threshold_to_diff ─────────────────────────────────────────────────
// Post-process diff buffer: pixels with max channel diff <= threshold become dark gray.
// Returns (exceed_count, total_count).

static std::pair<int,int> apply_threshold_to_diff(
    const ImageEntry& imgA, const ImageEntry& imgB,
    uint8_t* buf, int view_w, int view_h,
    const ViewportState& vp, int threshold, ChannelMode channel)
{
    if (threshold <= 0) return {0, 0};

    float half_vw = view_w * 0.5f, half_vh = view_h * 0.5f;
    float half_iw = imgA.width * 0.5f, half_ih = imgA.height * 0.5f;
    float th_norm = threshold / 255.0f;
    int exceed = 0, total = 0;

    for (int sy = 0; sy < view_h; ++sy) {
        for (int sx = 0; sx < view_w; ++sx) {
            float img_fx = (sx - half_vw) / vp.zoom - vp.pan_x + half_iw;
            float img_fy = (sy - half_vh) / vp.zoom - vp.pan_y + half_ih;

            if (img_fx < 0 || img_fy < 0 || img_fx >= imgA.width || img_fy >= imgA.height)
                continue;

            // Get original pixel diff (before amplification)
            float a[4], b_val[4];
            sample_pixel(imgA, img_fx, img_fy, vp.zoom, a);
            sample_pixel(imgB, img_fx, img_fy, vp.zoom, b_val);
            float max_diff;
            if      (channel == ChannelMode::Red)   max_diff = std::fabs(a[0] - b_val[0]);
            else if (channel == ChannelMode::Green) max_diff = std::fabs(a[1] - b_val[1]);
            else if (channel == ChannelMode::Blue)  max_diff = std::fabs(a[2] - b_val[2]);
            else {
                max_diff = 0.0f;
                for (int c = 0; c < 3; ++c) {
                    float d = std::fabs(a[c] - b_val[c]);
                    if (d > max_diff) max_diff = d;
                }
            }

            ++total;
            if (max_diff > th_norm) {
                ++exceed;
            } else {
                // Below threshold: dark gray
                uint8_t* dst = buf + (sy * view_w + sx) * 4;
                dst[0] = 40; dst[1] = 40; dst[2] = 40; dst[3] = 255;
            }
        }
    }
    return {exceed, total};
}

// ─── cpu_render_overlay ─────────────────────────────────────────────────────

void ImagePanel::cpu_render_overlay(const ImageEntry& imgA, const ImageEntry& imgB,
                                     const ViewportState& vp, uint8_t* buf,
                                     int view_w, int view_h,
                                     const OverlayState& overlay, ChannelMode channel) {
    float half_vw = view_w * 0.5f, half_vh = view_h * 0.5f;
    float half_iw = imgA.width * 0.5f, half_ih = imgA.height * 0.5f;

    for (int sy = 0; sy < view_h; ++sy) {
        for (int sx = 0; sx < view_w; ++sx) {
            float img_fx = (sx - half_vw) / vp.zoom - vp.pan_x + half_iw;
            float img_fy = (sy - half_vh) / vp.zoom - vp.pan_y + half_ih;
            uint8_t* dst = buf + (sy * view_w + sx) * 4;

            if (img_fx < 0 || img_fy < 0 || img_fx >= imgA.width || img_fy >= imgA.height) {
                dst[0] = 38; dst[1] = 38; dst[2] = 38; dst[3] = 255;
                continue;
            }

            float a[4], b_val[4];
            sample_pixel(imgA, img_fx, img_fy, vp.zoom, a);
            sample_pixel(imgB, img_fx, img_fy, vp.zoom, b_val);

            float result[4];
            if (overlay.mode == OverlayState::Mode::Curtain) {
                float uv_x = static_cast<float>(sx) / view_w;
                const float* pick = (uv_x < overlay.curtain_x) ? a : b_val;
                for (int c = 0; c < 4; ++c) result[c] = pick[c];
            } else {
                float alpha = overlay.alpha;
                for (int c = 0; c < 4; ++c)
                    result[c] = a[c] * (1.0f - alpha) + b_val[c] * alpha;
            }

            for (int c = 0; c < 4; ++c)
                dst[c] = (uint8_t)std::clamp(result[c] * 255.0f, 0.0f, 255.0f);

            apply_channel_grid(dst, img_fx, img_fy, vp.zoom, channel);
        }
    }
}

// ─── render_diff_software ─────────────────────────────────────────────────────

void ImagePanel::render_diff_software(AppState& state) {
    int idx_a = state.swap_images ? 1 : 0;
    int idx_b = state.swap_images ? 0 : 1;
    const ImageEntry& imgA = state.images[idx_a];
    const ImageEntry& imgB = state.images[idx_b];
    ViewportState& vp = state.views[0];

    if (!imgA.loaded || !imgB.loaded || imgA.pixels.empty() || imgB.pixels.empty()) {
        ImGui::TextDisabled("(need two images for diff)");
        return;
    }

    // Compute diff pixel list (lazy, cached by image dimensions)
    compute_diff_pixel_list(imgA, imgB, state.diff_listing);

    // Enhance mode: compute global min/max of non-zero diff
    if (state.diff.mode == DiffState::Mode::Enhance)
        compute_enhance_range(state.diff_listing, state.diff);
    float enh_min_f = state.diff.enhance_max > 0 ? state.diff.enhance_min / 255.0f : 0.0f;
    float enh_max_f = state.diff.enhance_max / 255.0f;

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int pw = std::max(1, (int)avail.x);
    int ph = std::max(1, (int)avail.y);

    if (vp.fit) viewport_fit(vp, imgA.width, imgA.height, pw, ph);
    viewport_clamp_pan(vp, imgA.width, imgA.height, pw, ph);

    SDL_Texture* tex = ensure_soft_texture(pw, ph);
    if (!tex) return;

    cpu_render_diff(imgA, imgB, vp, soft_buf_.data(), pw, ph,
                    state.diff.mode, state.diff.amplify, state.channel_mode,
                    enh_min_f, enh_max_f, state.diff.alpha);

    // Highlight mode: count non-zero diff pixels for status bar.
    // Skip when threshold>0 — the threshold pass below unconditionally overwrites
    // threshold_exceed_count/total_count, so this full viewport pass would be pure
    // wasted work with no effect on the buffer or the displayed counts.
    if (state.diff.mode == DiffState::Mode::Highlight && state.diff.threshold == 0) {
        auto [nonzero, total] = count_nonzero_diff_pixels(
            imgA, imgB, pw, ph, vp);
        state.diff.threshold_exceed_count = nonzero;
        state.diff.threshold_total_count  = total;
    }

    // Apply tolerance threshold post-processing
    if (state.diff.threshold > 0) {
        auto [exceed, total] = apply_threshold_to_diff(
            imgA, imgB, soft_buf_.data(), pw, ph, vp, state.diff.threshold, state.channel_mode);
        state.diff.threshold_exceed_count = exceed;
        state.diff.threshold_total_count  = total;
    }

    SDL_UpdateTexture(tex, nullptr, soft_buf_.data(), pw * 4);

    ImTextureID imgui_tex = reinterpret_cast<ImTextureID>(tex);
    ImGui::Image(imgui_tex, avail, ImVec2(0, 0), ImVec2(1, 1));
    bool img_hovered = ImGui::IsItemHovered();

    ImVec2 widget_pos = ImGui::GetItemRectMin();
    if (state.roi.active) {
        handle_roi_drag(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);
    } else {
        handle_mouse_pan(state, 0);
        handle_mouse_double_click(state, 0, widget_pos, pw, ph,
                                   imgA.width, imgA.height);
        handle_mouse_right_select(state, 0, widget_pos, pw, ph,
                                   imgA.width, imgA.height, 2);
    }

    if (state.show_borders)
        draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                          (float)pw, (float)ph,
                          (float)imgA.width, (float)imgA.height,
                          vp.pan_x, vp.pan_y, vp.zoom,
                          static_cast<ImU32>(state.border_colors[2]));

    if (state.roi.has_roi || state.roi.dragging)
        render_roi_overlay(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);

    if (vp.zoom >= 32.0f && imgA.loaded && imgB.loaded)
        render_diff_pixel_values(state, widget_pos, pw, ph);

    // "Identical" overlay when images are the same (channel-aware)
    {
        bool ch_identical = state.diff_listing.identical;
        if (!ch_identical && state.channel_mode != ChannelMode::RGB) {
            if (state.channel_mode == ChannelMode::Red)   ch_identical = state.diff_listing.identical_r;
            if (state.channel_mode == ChannelMode::Green) ch_identical = state.diff_listing.identical_g;
            if (state.channel_mode == ChannelMode::Blue)  ch_identical = state.diff_listing.identical_b;
        }
        if (ch_identical && state.diff.mode != DiffState::Mode::None
                         && state.diff.mode != DiffState::Mode::AlphaBlend) {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            const char* label = "Identical";
            float base_size = ImGui::GetFontSize();
            ImVec2 base_text = ImGui::CalcTextSize(label);
            float target_w = pw * 0.7f;
            float scale = target_w / base_text.x;
            float font_size = std::min(base_size * scale, 200.0f);
            ImVec2 text_sz = ImGui::GetFont()->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label);
            float tx = widget_pos.x + (pw - text_sz.x) * 0.5f;
            float ty = widget_pos.y + (ph - text_sz.y) * 0.5f;
            dl->AddText(ImGui::GetFont(), font_size, ImVec2(tx+2, ty+2), IM_COL32(0,0,0,160), label);
            dl->AddText(ImGui::GetFont(), font_size, ImVec2(tx, ty), IM_COL32(0,255,120,220), label);
        }
    }

    update_mouse_constraint(state, 0, widget_pos, pw, ph,
                            imgA.width, imgA.height, img_hovered);
    render_magnifier(state, 0, widget_pos, pw, ph,
                     imgA.width, imgA.height, true, img_hovered);
    render_crosshair(state, 0, widget_pos, pw, ph, imgA.width, imgA.height, true);
}

// ─── render_overlay_software ──────────────────────────────────────────────────

void ImagePanel::render_overlay_software(AppState& state) {
    int idx_a = state.swap_images ? 1 : 0;
    int idx_b = state.swap_images ? 0 : 1;
    const ImageEntry& imgA = state.images[idx_a];
    const ImageEntry& imgB = state.images[idx_b];
    ViewportState& vp = state.views[0];

    if (!imgA.loaded || !imgB.loaded || imgA.pixels.empty() || imgB.pixels.empty()) {
        ImGui::TextDisabled("(overlay: need two images)");
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int pw = std::max(1, (int)avail.x);
    int ph = std::max(1, (int)avail.y);

    if (vp.fit) viewport_fit(vp, imgA.width, imgA.height, pw, ph);
    viewport_clamp_pan(vp, imgA.width, imgA.height, pw, ph);

    SDL_Texture* tex = ensure_soft_texture(pw, ph);
    if (!tex) return;

    cpu_render_overlay(imgA, imgB, vp, soft_buf_.data(), pw, ph,
                       state.overlay, state.channel_mode);

    SDL_UpdateTexture(tex, nullptr, soft_buf_.data(), pw * 4);

    ImTextureID imgui_tex = reinterpret_cast<ImTextureID>(tex);
    ImGui::Image(imgui_tex, avail, ImVec2(0, 0), ImVec2(1, 1));

    ImVec2 widget_pos = ImGui::GetItemRectMin();

    // Curtain mode: draw divider line + drag handling
    if (state.overlay.mode == OverlayState::Mode::Curtain) {
        float curtain_sx = widget_pos.x + state.overlay.curtain_x * pw;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(curtain_sx, widget_pos.y),
                    ImVec2(curtain_sx, widget_pos.y + ph),
                    IM_COL32(255, 255, 255, 200), 2.0f);
        char lbl[16];
        std::snprintf(lbl, sizeof(lbl), "A | B");
        dl->AddText(ImVec2(curtain_sx + 4, widget_pos.y + 4),
                    IM_COL32(255, 255, 255, 200), lbl);

        if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            float mx = ImGui::GetMousePos().x;
            state.overlay.curtain_x = std::clamp((mx - widget_pos.x) / pw, 0.0f, 1.0f);
        }
    } else {
        handle_mouse_pan(state, 0);
        handle_mouse_double_click(state, 0, widget_pos, pw, ph,
                                   imgA.width, imgA.height);
    }

    if (state.roi.active) {
        handle_roi_drag(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);
    } else if (state.overlay.mode != OverlayState::Mode::Curtain) {
        handle_mouse_right_select(state, 0, widget_pos, pw, ph,
                                   imgA.width, imgA.height);
    }

    if (state.show_borders)
        draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                          (float)pw, (float)ph,
                          (float)imgA.width, (float)imgA.height,
                          vp.pan_x, vp.pan_y, vp.zoom,
                          IM_COL32(150, 255, 150, 200));

    if (state.roi.has_roi || state.roi.dragging)
        render_roi_overlay(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);

    render_crosshair(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);
    render_pathfinder(state, 0, widget_pos, pw, ph);
}

// ─── render_heatmap_software (SSIM / FLIP) ────────────────────────────────────

void ImagePanel::render_heatmap_software(AppState& state) {
    const ImageEntry& imgA = state.images[0];
    ViewportState& vp = state.views[0];

    // SSIM and FLIP share this path; pick the active mode's heatmap fields.
    const bool is_flip = (state.diff.mode == DiffState::Mode::FLIP);
    uintptr_t             heat_tex = is_flip ? state.diff.flip_texture_id : state.diff.ssim_texture_id;
    std::vector<uint8_t>& heat_px  = is_flip ? state.diff.flip_pixels     : state.diff.ssim_pixels;
    int                   heat_w   = is_flip ? state.diff.flip_w          : state.diff.ssim_w;
    int                   heat_h   = is_flip ? state.diff.flip_h          : state.diff.ssim_h;

    if (!imgA.loaded || heat_tex == 0) {
        ImGui::TextDisabled("(heatmap not ready)");
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int pw = std::max(1, (int)avail.x);
    int ph = std::max(1, (int)avail.y);

    if (vp.fit) viewport_fit(vp, imgA.width, imgA.height, pw, ph);
    viewport_clamp_pan(vp, imgA.width, imgA.height, pw, ph);

    SDL_Texture* tex = ensure_soft_texture(pw, ph);
    if (!tex) return;

    // Use stored colored RGBA pixels with cpu_render_image for viewport transform
    if (!heat_px.empty() && heat_w > 0) {
        ImageEntry fake{};
        fake.loaded  = true;
        fake.width   = heat_w;
        fake.height  = heat_h;
        fake.pixels.swap(heat_px);  // zero-copy swap
        cpu_render_image(fake, vp, soft_buf_.data(), pw, ph);
        fake.pixels.swap(heat_px);  // swap back
    } else {
        // Fallback: clear to dark
        std::memset(soft_buf_.data(), 26, soft_buf_.size());
    }

    SDL_UpdateTexture(tex, nullptr, soft_buf_.data(), pw * 4);

    ImTextureID imgui_tex = reinterpret_cast<ImTextureID>(tex);
    ImGui::Image(imgui_tex, avail, ImVec2(0, 0), ImVec2(1, 1));

    ImVec2 widget_pos = ImGui::GetItemRectMin();
    if (state.roi.active) {
        handle_roi_drag(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);
    } else {
        handle_mouse_pan(state, 0);
        handle_mouse_double_click(state, 0, widget_pos, pw, ph,
                                   imgA.width, imgA.height);
        handle_mouse_right_select(state, 0, widget_pos, pw, ph,
                                   imgA.width, imgA.height, 2);
    }

    if (state.show_borders)
        draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                          (float)pw, (float)ph,
                          (float)imgA.width, (float)imgA.height,
                          vp.pan_x, vp.pan_y, vp.zoom,
                          static_cast<ImU32>(state.border_colors[2]));

    if (state.roi.has_roi || state.roi.dragging)
        render_roi_overlay(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);

    render_crosshair(state, 0, widget_pos, pw, ph, imgA.width, imgA.height, true);
}

// ─── Mouse pan helper ─────────────────────────────────────────────────────────

void ImagePanel::handle_mouse_pan(AppState& state, int panel_idx) {
    if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);

        // Convert screen-pixel drag to image-pixel pan
        float dx = delta.x;
        float dy = delta.y;

        auto& v = state.views[panel_idx];
        v.pan_x = std::round(v.pan_x + dx / v.zoom);
        v.pan_y = std::round(v.pan_y + dy / v.zoom);
        v.fit    = false;

        if (state.sync_viewports) {
            auto& ov = state.views[1 - panel_idx];
            ov.pan_x = v.pan_x;
            ov.pan_y = v.pan_y;
        }
    }

    // Scroll to zoom — Alt+Wheel: 시퀀스 네비게이션 (커서 아래 패널 대상)
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            if (ImGui::GetIO().KeyAlt) {
                sequence_navigate(state, panel_idx, (wheel > 0) ? +1 : -1);
            } else {
                auto& v = state.views[panel_idx];
                if (wheel > 0) viewport_zoom_in(v);
                else           viewport_zoom_out(v);
                if (state.sync_viewports) {
                    auto& ov = state.views[1 - panel_idx];
                    ov.zoom = v.zoom;
                    ov.fit  = false;
                }
            }
        }
    }
}

// ─── Double-click zoom ───────────────────────────────────────────────────────

void ImagePanel::handle_mouse_double_click(AppState& state, int panel_idx,
                                           ImVec2 widget_pos, int view_w, int view_h,
                                           int img_w, int img_h) {
    bool any_dbl   = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
                  || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right);
    // ImGui의 ConfigMacOSXBehaviors가 Ctrl↔Cmd 스왑 + Ctrl+Left→Right 변환을
    // 하므로, SDL API로 물리 키/버튼 상태를 직접 확인
    SDL_Keymod mods = SDL_GetModState();
    bool ctrl_held  = (mods & SDL_KMOD_CTRL) != 0;
    bool phys_left  = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
    bool left_dbl   = any_dbl && phys_left;
    bool right_dbl  = any_dbl && !phys_left;
    if (!ImGui::IsItemHovered() || !(left_dbl || right_dbl))
        return;

    auto& v = state.views[panel_idx];
    ImVec2 mouse = ImGui::GetMousePos();

    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img_w  * 0.5f;
    float half_ih = img_h  * 0.5f;

    // 스크린 좌표 → 이미지 픽셀 좌표
    float img_x = (mouse.x - widget_pos.x - half_vw) / v.zoom - v.pan_x + half_iw;
    float img_y = (mouse.y - widget_pos.y - half_vh) / v.zoom - v.pan_y + half_ih;

    // 점진적 줌: Ctrl 여부에 따라 16배/2배, 좌=확대 우=축소
    float new_zoom;
    if (ctrl_held && left_dbl) {
        new_zoom = v.zoom * 16.0f;
    } else if (ctrl_held && right_dbl) {
        new_zoom = v.zoom / 16.0f;
    } else if (left_dbl) {
        new_zoom = v.zoom * 2.0f;
    } else {
        new_zoom = v.zoom * 0.5f;
    }
    viewport_set_zoom(v, new_zoom);
    v.pan_x = half_iw - img_x;
    v.pan_y = half_ih - img_y;
    v.fit   = false;

    if (state.sync_viewports) {
        auto& ov = state.views[1 - panel_idx];
        viewport_set_zoom(ov, new_zoom);
        ov.pan_x = v.pan_x;
        ov.pan_y = v.pan_y;
        ov.fit   = false;
    }
}

// ─── Right-click context menu ─────────────────────────────────────────────────

void ImagePanel::render_context_popup(AppState& state, int /*context_type*/) {
    if (!ImGui::BeginPopup("##PanelContextMenu")) return;

    if (ImGui::MenuItem("Save As...")) {
        open_context_save_dialog(state, context_panel_type_);
    }
    if (ImGui::MenuItem("Copy to Clipboard")) {
        copy_panel_to_clipboard(state, context_panel_type_);
    }
    ImGui::EndPopup();
}

void ImagePanel::copy_panel_to_clipboard(AppState& state, int target_type) {
    clipboard_copy_image(state, target_type);
}

// ─── Right-click drag-to-zoom ─────────────────────────────────────────────────
// Allows the user to drag-select a region with the right mouse button.
// On release the view snaps to the largest 2^n zoom that fits the selection.

void ImagePanel::handle_mouse_right_select(AppState& state, int panel_idx,
                                            ImVec2 widget_pos,
                                            int view_w, int view_h,
                                            int img_w, int img_h,
                                            int context_type) {
    // ★ Always render popup (before any early return)
    if (context_type >= 0)
        render_context_popup(state, context_type);

    // Begin drag when right-click starts on this panel's image widget
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        drag_selecting_   = true;
        drag_start_       = ImGui::GetMousePos();
        drag_panel_idx_   = panel_idx;
        right_press_time_ = ImGui::GetTime();
    }

    // Only process/draw for the panel that owns the drag
    if (!drag_selecting_ || panel_idx != drag_panel_idx_) return;

    ImVec2 curr = ImGui::GetMousePos();
    float rx0 = std::min(drag_start_.x, curr.x);
    float ry0 = std::min(drag_start_.y, curr.y);
    float rx1 = std::max(drag_start_.x, curr.x);
    float ry1 = std::max(drag_start_.y, curr.y);

    // Draw selection overlay on foreground layer
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(rx0, ry0), ImVec2(rx1, ry1),
                      IM_COL32(255, 255, 255, 25));
    dl->AddRect(ImVec2(rx0, ry0), ImVec2(rx1, ry1),
                IM_COL32(255, 220, 50, 220), 0.0f, 0, 1.5f);

    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Right)) return;

    drag_selecting_ = false;

    // Tiny drag → long-press (≥0.5s, no Ctrl) opens context menu
    if ((rx1 - rx0) < 5.0f || (ry1 - ry0) < 5.0f) {
        double hold = ImGui::GetTime() - right_press_time_;
        if (context_type >= 0 && hold >= 0.5 && !ImGui::GetIO().KeyCtrl) {
            context_panel_type_ = context_type;
            ImGui::OpenPopup("##PanelContextMenu");
        }
        return;
    }

    ViewportState& v = state.views[panel_idx];
    float zoom    = v.zoom;
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img_w  * 0.5f;
    float half_ih = img_h  * 0.5f;

    // Screen (absolute) → image-pixel coordinate
    // img_px = (abs_screen - widget_pos - half_view) / zoom - pan + half_img
    auto s2ix = [&](float sx) {
        return (sx - widget_pos.x - half_vw) / zoom - v.pan_x + half_iw;
    };
    auto s2iy = [&](float sy) {
        return (sy - widget_pos.y - half_vh) / zoom - v.pan_y + half_ih;
    };

    float ix0 = s2ix(rx0), iy0 = s2iy(ry0);
    float ix1 = s2ix(rx1), iy1 = s2iy(ry1);
    float sel_w = ix1 - ix0;
    float sel_h = iy1 - iy0;
    if (sel_w < 1.0f || sel_h < 1.0f) return;

    // Largest power-of-2 zoom that fits the selection
    float need_zoom = std::min(static_cast<float>(view_w) / sel_w,
                               static_cast<float>(view_h) / sel_h);
    float new_zoom  = std::pow(2.0f, std::floor(std::log2(need_zoom)));
    new_zoom = std::max(ZOOM_LEVELS[0],
               std::min(ZOOM_LEVELS[NUM_ZOOM_LEVELS - 1], new_zoom));

    // Pan so selection centre maps to viewport centre
    float sel_cx = (ix0 + ix1) * 0.5f;
    float sel_cy = (iy0 + iy1) * 0.5f;

    viewport_set_zoom(v, new_zoom);
    v.pan_x = half_iw - sel_cx;
    v.pan_y = half_ih - sel_cy;
    v.fit   = false;

    if (state.sync_viewports) {
        ViewportState& ov = state.views[1 - panel_idx];
        viewport_set_zoom(ov, new_zoom);
        ov.pan_x = v.pan_x;
        ov.pan_y = v.pan_y;
        ov.fit   = false;
    }
}

// ─── draw_image_border ────────────────────────────────────────────────────────
// Draws a thick border rect around the image extent in screen space.
// Transform (matches shader): screen_px = (img_px + pan - half_img) * zoom + half_view

static void draw_image_border(ImDrawList* dl, ImVec2 widget_pos,
                               float view_w, float view_h,
                               float img_w,  float img_h,
                               float pan_x,  float pan_y, float zoom,
                               ImU32 border_col,
                               bool  active) {
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img_w  * 0.5f;
    float half_ih = img_h  * 0.5f;

    float x0 = widget_pos.x + (pan_x - half_iw) * zoom + half_vw;
    float y0 = widget_pos.y + (pan_y - half_ih) * zoom + half_vh;
    float x1 = widget_pos.x + (img_w + pan_x - half_iw) * zoom + half_vw;
    float y1 = widget_pos.y + (img_h + pan_y - half_ih) * zoom + half_vh;

    // Viewport bounds for clipping each edge independently
    float vx0 = widget_pos.x,          vy0 = widget_pos.y;
    float vx1 = widget_pos.x + view_w, vy1 = widget_pos.y + view_h;

    ImU32 shadow_col = IM_COL32(0, 0, 0, 160);

    // 활성 패널: 선 두께를 키워 시각적 차별화.
    float line_w   = active ? 4.0f : 2.0f;
    float shadow_w = active ? 5.0f : 3.0f;

    // Vertical edge: draw only if x is inside viewport; clamp y range
    auto draw_v_edge = [&](float ex, float ey0, float ey1) {
        if (ex < vx0 || ex > vx1) return;
        float cy0 = std::max(ey0, vy0), cy1 = std::min(ey1, vy1);
        if (cy0 >= cy1) return;
        dl->AddLine(ImVec2(ex + 1.0f, cy0 + 1.0f), ImVec2(ex + 1.0f, cy1 + 1.0f), shadow_col, shadow_w);
        dl->AddLine(ImVec2(ex,         cy0),         ImVec2(ex,         cy1),         border_col, line_w);
    };
    // Horizontal edge: draw only if y is inside viewport; clamp x range
    auto draw_h_edge = [&](float ey, float ex0, float ex1) {
        if (ey < vy0 || ey > vy1) return;
        float cx0 = std::max(ex0, vx0), cx1 = std::min(ex1, vx1);
        if (cx0 >= cx1) return;
        dl->AddLine(ImVec2(cx0 + 1.0f, ey + 1.0f), ImVec2(cx1 + 1.0f, ey + 1.0f), shadow_col, shadow_w);
        dl->AddLine(ImVec2(cx0,         ey),         ImVec2(cx1,         ey),         border_col, line_w);
    };

    draw_v_edge(x0, y0, y1);  // left edge
    draw_v_edge(x1, y0, y1);  // right edge
    draw_h_edge(y0, x0, x1);  // top edge
    draw_h_edge(y1, x0, x1);  // bottom edge
}

// ─── render_crosshair ─────────────────────────────────────────────────────────

void ImagePanel::render_bad_pixel_overlay(const AppState& state, int panel_idx,
                                          ImVec2 widget_pos, int view_w, int view_h,
                                          int img_w, int img_h) {
    if (!state.show_bad_pixels) return;
    int actual = state.swap_images ? (1 - panel_idx) : panel_idx;
    const ImageEntry& img = state.images[actual];
    if (!img.loaded || img.pixels_f32.empty()) return;   // only float/HDR carries bad pixels

    const ViewportState& vp = state.views[panel_idx];
    float half_vw = view_w * 0.5f, half_vh = view_h * 0.5f;
    float half_iw = img_w * 0.5f,  half_ih = img_h * 0.5f;

    // Visible image-pixel bounds (invert the screen→image transform at the corners).
    auto to_img = [&](float sx, float sy, float& fx, float& fy) {
        fx = (sx - half_vw) / vp.zoom - vp.pan_x + half_iw;
        fy = (sy - half_vh) / vp.zoom - vp.pan_y + half_ih;
    };
    float fx0, fy0, fx1, fy1;
    to_img(0, 0, fx0, fy0); to_img((float)view_w, (float)view_h, fx1, fy1);
    int x0 = std::max(0, (int)std::floor(fx0)), x1 = std::min(img_w, (int)std::ceil(fx1) + 1);
    int y0 = std::max(0, (int)std::floor(fy0)), y1 = std::min(img_h, (int)std::ceil(fy1) + 1);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float rs = std::max(vp.zoom, 3.0f);
    int drawn = 0; const int MAXDRAW = 30000;
    for (int py = y0; py < y1 && drawn < MAXDRAW; ++py)
        for (int px = x0; px < x1 && drawn < MAXDRAW; ++px) {
            const float* p = &img.pixels_f32[(static_cast<size_t>(py)*img_w + px)*4];
            ImU32 col = 0;
            for (int c = 0; c < 3; ++c) {
                float v = p[c];
                if      (std::isnan(v)) { col = IM_COL32(255,0,255,220); break; }  // magenta
                else if (std::isinf(v)) { col = IM_COL32(255,0,0,220);   break; }  // red
                else if (v < 0.0f)      { col = IM_COL32(0,90,255,200); }          // blue
                else if (v > 1.0f && col == 0) col = IM_COL32(255,160,0,200);      // orange
            }
            if (col) {
                float sx = widget_pos.x + (px + 0.5f - half_iw + vp.pan_x)*vp.zoom + half_vw;
                float sy = widget_pos.y + (py + 0.5f - half_ih + vp.pan_y)*vp.zoom + half_vh;
                dl->AddRectFilled(ImVec2(sx - rs*0.5f, sy - rs*0.5f),
                                  ImVec2(sx + rs*0.5f, sy + rs*0.5f), col);
                ++drawn;
            }
        }
    dl->AddText(ImVec2(widget_pos.x + 6, widget_pos.y + 6), IM_COL32(255,255,255,235),
                "bad-px: NaN magenta / Inf red / neg blue / >1 orange");
}

void ImagePanel::render_crosshair(const AppState& state, int panel_idx,
                                   ImVec2 widget_pos, int view_w, int view_h,
                                   int img_w, int img_h,
                                   bool is_diff_panel) {
    if (!state.show_crosshair) return;

    ImVec2 mouse = ImGui::GetMousePos();
    // Only draw if mouse is within this panel
    if (mouse.x < widget_pos.x || mouse.x > widget_pos.x + view_w ||
        mouse.y < widget_pos.y || mouse.y > widget_pos.y + view_h)
        return;

    const ViewportState& vp = state.views[panel_idx];
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img_w * 0.5f;
    float half_ih = img_h * 0.5f;

    // Screen → image coordinate
    float screen_x = mouse.x - widget_pos.x;
    float screen_y = mouse.y - widget_pos.y;
    float img_fx = (screen_x - half_vw) / vp.zoom - vp.pan_x + half_iw;
    float img_fy = (screen_y - half_vh) / vp.zoom - vp.pan_y + half_ih;

    int img_x = static_cast<int>(std::floor(img_fx));
    int img_y = static_cast<int>(std::floor(img_fy));
    if (state.mouse_constrained) {
        img_x = std::clamp(img_x, 0, img_w - 1);
        img_y = std::clamp(img_y, 0, img_h - 1);
    }

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // Crosshair lines (full viewport span)
    ImU32 line_col = IM_COL32(255, 255, 0, 160);
    dl->AddLine(ImVec2(widget_pos.x, mouse.y),
                ImVec2(widget_pos.x + view_w, mouse.y), line_col, 1.0f);
    dl->AddLine(ImVec2(mouse.x, widget_pos.y),
                ImVec2(mouse.x, widget_pos.y + view_h), line_col, 1.0f);

    // Coordinate label near cursor
    if (img_x >= 0 && img_x < img_w && img_y >= 0 && img_y < img_h) {
        char coord_buf[32];
        std::snprintf(coord_buf, sizeof(coord_buf), "(%d, %d)", img_x, img_y);
        ImVec2 text_size = ImGui::CalcTextSize(coord_buf);
        float tx = mouse.x + 12.0f;
        float ty = mouse.y - text_size.y - 6.0f;
        // Keep label inside viewport
        if (tx + text_size.x + 4 > widget_pos.x + view_w)
            tx = mouse.x - text_size.x - 16.0f;
        if (ty < widget_pos.y)
            ty = mouse.y + 6.0f;
        dl->AddRectFilled(ImVec2(tx - 3, ty - 2),
                          ImVec2(tx + text_size.x + 3, ty + text_size.y + 2),
                          IM_COL32(0, 0, 0, 200), 3.0f);
        dl->AddText(ImVec2(tx, ty), IM_COL32(255, 255, 0, 240), coord_buf);

        // Pixel RGB value below coordinate
        int idx_a = state.swap_images ? 1 : 0;
        int idx_b = state.swap_images ? 0 : 1;
        const ImageEntry& imgA_cr = state.images[idx_a];
        const ImageEntry& imgB_cr = state.images[idx_b];
        int actual_idx = state.swap_images ? (1 - panel_idx) : panel_idx;
        const ImageEntry& img = state.images[actual_idx];

        char pv_buf[64];
        ImU32 pv_color = IM_COL32(200, 220, 255, 240);
        bool pv_drawn = false;

        if (is_diff_panel && imgA_cr.loaded && imgB_cr.loaded &&
            img_x < imgA_cr.width && img_y < imgA_cr.height &&
            img_x < imgB_cr.width && img_y < imgB_cr.height) {
            // Diff panel: |A-B| 값 표시
            char sr[16], sg[16], sb[16];
            if (imgA_cr.ppm_maxval > 0 && !imgA_cr.pixels_orig.empty() &&
                imgB_cr.ppm_maxval > 0 && !imgB_cr.pixels_orig.empty()) {
                int oa = (img_y * imgA_cr.width + img_x) * 3;
                int ob = (img_y * imgB_cr.width + img_x) * 3;
                int dr = std::abs((int)imgA_cr.pixels_orig[oa]   - (int)imgB_cr.pixels_orig[ob]);
                int dg = std::abs((int)imgA_cr.pixels_orig[oa+1] - (int)imgB_cr.pixels_orig[ob+1]);
                int db = std::abs((int)imgA_cr.pixels_orig[oa+2] - (int)imgB_cr.pixels_orig[ob+2]);
                fmt_int_pixel(sr, sizeof(sr), dr, state.pixel_format);
                fmt_int_pixel(sg, sizeof(sg), dg, state.pixel_format);
                fmt_int_pixel(sb, sizeof(sb), db, state.pixel_format);
                std::snprintf(pv_buf, sizeof(pv_buf), "[%s, %s, %s]", sr, sg, sb);
                pv_color = IM_COL32(0, 255, 255, 240);  // cyan
                pv_drawn = true;
            } else if (!imgA_cr.pixels.empty() && !imgB_cr.pixels.empty()) {
                int oa = (img_y * imgA_cr.width + img_x) * 4;
                int ob = (img_y * imgB_cr.width + img_x) * 4;
                int dr = std::abs((int)imgA_cr.pixels[oa]   - (int)imgB_cr.pixels[ob]);
                int dg = std::abs((int)imgA_cr.pixels[oa+1] - (int)imgB_cr.pixels[ob+1]);
                int db = std::abs((int)imgA_cr.pixels[oa+2] - (int)imgB_cr.pixels[ob+2]);
                fmt_int_pixel(sr, sizeof(sr), dr, state.pixel_format);
                fmt_int_pixel(sg, sizeof(sg), dg, state.pixel_format);
                fmt_int_pixel(sb, sizeof(sb), db, state.pixel_format);
                std::snprintf(pv_buf, sizeof(pv_buf), "[%s, %s, %s]", sr, sg, sb);
                pv_color = IM_COL32(0, 255, 255, 240);  // cyan
                pv_drawn = true;
            }
        }
        if (!pv_drawn && img.loaded && !img.pixels.empty()) {
            // 일반 패널: 이미지 원본 픽셀값 표시
            char sr[16], sg[16], sb[16];
            if (img.ppm_maxval > 0 && !img.pixels_orig.empty()) {
                int oidx = (img_y * img.width + img_x) * 3;
                fmt_int_pixel(sr, sizeof(sr), img.pixels_orig[oidx],   state.pixel_format);
                fmt_int_pixel(sg, sizeof(sg), img.pixels_orig[oidx+1], state.pixel_format);
                fmt_int_pixel(sb, sizeof(sb), img.pixels_orig[oidx+2], state.pixel_format);
            } else {
                int off = (img_y * img.width + img_x) * 4;
                fmt_int_pixel(sr, sizeof(sr), img.pixels[off],   state.pixel_format);
                fmt_int_pixel(sg, sizeof(sg), img.pixels[off+1], state.pixel_format);
                fmt_int_pixel(sb, sizeof(sb), img.pixels[off+2], state.pixel_format);
            }
            std::snprintf(pv_buf, sizeof(pv_buf), "%s, %s, %s", sr, sg, sb);
            pv_drawn = true;
        }
        if (pv_drawn) {
            ImVec2 pv_size = ImGui::CalcTextSize(pv_buf);
            float pv_y = ty + text_size.y + 2.0f;
            dl->AddRectFilled(ImVec2(tx - 3, pv_y - 1),
                              ImVec2(tx + pv_size.x + 3, pv_y + pv_size.y + 1),
                              IM_COL32(0, 0, 0, 200), 3.0f);
            dl->AddText(ImVec2(tx, pv_y), pv_color, pv_buf);
        }
    }

    // If sync viewports, draw crosshair on OTHER panel too (using same image coords)
    if (state.sync_viewports && state.images[0].loaded && state.images[1].loaded) {
        // The other panel's crosshair will be drawn when render_crosshair is called
        // for the other panel, but we need to show the crosshair at the same IMAGE
        // position (not screen position). This is handled by checking whether the
        // mouse is hovering a different panel and converting via image coords.
        // For simplicity, we rely on the foreground draw list being shared — the
        // calling code handles both panels.
    }
}

// ─── update_mouse_constraint ──────────────────────────────────────────────────

void ImagePanel::update_mouse_constraint(AppState& state, int panel_idx,
                                          ImVec2 widget_pos, int view_w, int view_h,
                                          int img_w, int img_h, bool img_hovered) {
    ImGuiIO& io = ImGui::GetIO();
    bool want = state.magnifier_active && io.KeyCtrl && img_hovered;

    if (!want) return;  // 해제는 frame-end / Ctrl+M 토글에서 처리

    // 이미지의 화면 좌표 계산 (draw_image_border와 동일한 변환)
    const ViewportState& vp = state.views[panel_idx];
    float half_vw = view_w * 0.5f, half_vh = view_h * 0.5f;
    float half_iw = img_w  * 0.5f, half_ih = img_h  * 0.5f;

    float x0 = widget_pos.x + (vp.pan_x - half_iw) * vp.zoom + half_vw;
    float y0 = widget_pos.y + (vp.pan_y - half_ih) * vp.zoom + half_vh;
    float x1 = x0 + img_w * vp.zoom;
    float y1 = y0 + img_h * vp.zoom;

    // 이미지를 부분적으로라도 커버하는 스크린 픽셀을 모두 포함
    int cx0 = (int)std::floor(std::max(x0, widget_pos.x));
    int cy0 = (int)std::floor(std::max(y0, widget_pos.y));
    int cx1 = (int)std::floor(std::min(x1, widget_pos.x + (float)view_w)) + 1;
    int cy1 = (int)std::floor(std::min(y1, widget_pos.y + (float)view_h)) + 1;
    // 위젯 경계 초과 방지
    cx0 = std::max(cx0, (int)widget_pos.x);
    cy0 = std::max(cy0, (int)widget_pos.y);
    cx1 = std::min(cx1, (int)widget_pos.x + view_w);
    cy1 = std::min(cy1, (int)widget_pos.y + view_h);

    if (cx1 <= cx0 || cy1 <= cy0) return;  // degenerate rect — 무시

    SDL_Rect rect = { cx0, cy0, cx1 - cx0, cy1 - cy0 };
    SDL_SetWindowMouseRect(state.window, &rect);
    state.mouse_constrained = true;
}

// ─── render_magnifier ─────────────────────────────────────────────────────────
// Ctrl+5 홀드 + 마우스 호버 → 16×16 픽셀 영역을 16배 확대한 매그니파이어 툴팁

void ImagePanel::render_magnifier(const AppState& state, int panel_idx,
                                   ImVec2 widget_pos, int view_w, int view_h,
                                   int img_w, int img_h, bool is_diff_panel,
                                   bool img_hovered) {
    // Ctrl+M 토글로 모든 모드에서 제어
    // 32x 이상 줌에서는 개별 픽셀이 충분히 크므로 자동 숨김
    const ViewportState& vp = state.views[panel_idx];
    bool show_mag = img_hovered && vp.zoom < 32.0f && state.magnifier_active;
    if (!show_mag) return;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = ImGui::GetMousePos();
    if (mouse.x < widget_pos.x || mouse.x > widget_pos.x + view_w ||
        mouse.y < widget_pos.y || mouse.y > widget_pos.y + view_h)
        return;
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img_w * 0.5f;
    float half_ih = img_h * 0.5f;

    // Screen → image coordinate
    float screen_x = mouse.x - widget_pos.x;
    float screen_y = mouse.y - widget_pos.y;
    float img_fx = (screen_x - half_vw) / vp.zoom - vp.pan_x + half_iw;
    float img_fy = (screen_y - half_vh) / vp.zoom - vp.pan_y + half_ih;

    int center_x = static_cast<int>(std::floor(img_fx));
    int center_y = static_cast<int>(std::floor(img_fy));

    if (state.mouse_constrained) {
        // 제한 모드: 가장자리 1px 밖은 마지막 유효 픽셀로 클램핑
        center_x = std::clamp(center_x, 0, img_w - 1);
        center_y = std::clamp(center_y, 0, img_h - 1);
    } else if (center_x < 0 || center_x >= img_w || center_y < 0 || center_y >= img_h) {
        return;
    }

    const int radius = 8;          // ±8 = 16×16 영역
    const int cell_size = 32;      // 각 픽셀을 32×32로 확대 (32배 줌)
    const int grid_size = radius * 2;  // 16
    const float tooltip_size = static_cast<float>(grid_size * cell_size);  // 512

    // 툴팁 위치: 마우스 오른쪽 아래, 화면 밖으로 나가면 조정
    float tx = mouse.x + 20.0f;
    float ty = mouse.y + 20.0f;
    ImVec2 display_size = io.DisplaySize;
    if (tx + tooltip_size + 4 > display_size.x)
        tx = mouse.x - tooltip_size - 20.0f;
    if (ty + tooltip_size + 4 > display_size.y)
        ty = mouse.y - tooltip_size - 20.0f;
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // 배경
    dl->AddRectFilled(ImVec2(tx - 2, ty - 2),
                      ImVec2(tx + tooltip_size + 2, ty + tooltip_size + 2),
                      IM_COL32(0, 0, 0, 255), 4.0f);

    int idx_a = state.swap_images ? 1 : 0;
    int idx_b = state.swap_images ? 0 : 1;
    const ImageEntry& imgA = state.images[idx_a];
    const ImageEntry& imgB = state.images[idx_b];

    int actual_idx = state.swap_images ? (1 - panel_idx) : panel_idx;
    const ImageEntry& img = state.images[actual_idx];

    for (int dy = -radius; dy < radius; ++dy) {
        for (int dx = -radius; dx < radius; ++dx) {
            int px = center_x + dx;
            int py = center_y + dy;

            uint8_t r = 0, g = 0, b = 0;

            if (px >= 0 && px < img_w && py >= 0 && py < img_h) {
                if (is_diff_panel && imgA.loaded && imgB.loaded &&
                    px < imgA.width && py < imgA.height &&
                    px < imgB.width && py < imgB.height &&
                    ((!imgA.pixels.empty() && !imgB.pixels.empty()) ||
                     (!imgA.pixels_f32.empty() && !imgB.pixels_f32.empty()))) {
                    // SSIM/FLIP: precomputed heatmap에서 직접 읽기
                    if (state.diff.mode == DiffState::Mode::SSIM ||
                        state.diff.mode == DiffState::Mode::FLIP) {
                        bool is_flip = (state.diff.mode == DiffState::Mode::FLIP);
                        const std::vector<uint8_t>& hpx = is_flip ? state.diff.flip_pixels : state.diff.ssim_pixels;
                        int hw = is_flip ? state.diff.flip_w : state.diff.ssim_w;
                        int hh = is_flip ? state.diff.flip_h : state.diff.ssim_h;
                        if (!hpx.empty() && px < hw && py < hh) {
                            int soff = (py * hw + px) * 4;
                            r = hpx[soff + 0];
                            g = hpx[soff + 1];
                            b = hpx[soff + 2];
                        }
                    } else {
                        // |A-B| 계산 (float [0,1]) — pixels_f32 우선, pixels 폴백
                        int off_a = (py * imgA.width + px) * 4;
                        int off_b = (py * imgB.width + px) * 4;
                        float diff[3], aVal[3];

                        if (!imgA.pixels_f32.empty() && !imgB.pixels_f32.empty()) {
                            for (int c = 0; c < 3; ++c) {
                                aVal[c] = imgA.pixels_f32[off_a + c];
                                diff[c] = std::fabs(imgA.pixels_f32[off_a + c] - imgB.pixels_f32[off_b + c]);
                            }
                        } else {
                            for (int c = 0; c < 3; ++c) {
                                aVal[c] = imgA.pixels[off_a + c] / 255.0f;
                                diff[c] = std::fabs(imgA.pixels[off_a + c] / 255.0f - imgB.pixels[off_b + c] / 255.0f);
                            }
                        }

                        if (state.diff.mode == DiffState::Mode::PixelRelative) {
                            constexpr float eps = 0.001f;
                            for (int c = 0; c < 3; ++c)
                                diff[c] = diff[c] / std::max(aVal[c] + eps, eps);
                        }

                        float amp = state.diff.amplify;
                        if (state.diff.mode == DiffState::Mode::Highlight) {
                            float max_d = std::max({diff[0], diff[1], diff[2]}) * amp;
                            if (max_d > 0.0f) {
                                float bright = 0.502f + std::clamp(max_d, 0.0f, 1.0f) * 0.498f;
                                r = (uint8_t)(bright * 255); g = 0; b = 0;
                            }
                        } else if (state.diff.mode == DiffState::Mode::FalseColor) {
                            float intensity = (diff[0] + diff[1] + diff[2]) / 3.0f;
                            uint8_t fc[3];
                            falsecolor(intensity * amp, fc);
                            r = fc[0]; g = fc[1]; b = fc[2];
                        } else if (state.diff.mode == DiffState::Mode::Enhance) {
                            int emin = state.diff.enhance_min;
                            int emax = state.diff.enhance_max;
                            int range = (emax > emin) ? (emax - emin) : 1;
                            bool all_zero = (diff[0] == 0.0f && diff[1] == 0.0f && diff[2] == 0.0f);
                            if (!all_zero) {
                                auto remap_ch = [&](float d) -> uint8_t {
                                    if (d == 0.0f) return 0;
                                    int iv = (int)(d * 255.0f);
                                    return (uint8_t)std::clamp(128 + (iv - emin) * 127 / range, 0, 255);
                                };
                                r = remap_ch(diff[0]); g = remap_ch(diff[1]); b = remap_ch(diff[2]);
                            }
                        } else {
                            // PixelAbsolute (default)
                            r = (uint8_t)(std::clamp(diff[0]*amp, 0.0f, 1.0f) * 255);
                            g = (uint8_t)(std::clamp(diff[1]*amp, 0.0f, 1.0f) * 255);
                            b = (uint8_t)(std::clamp(diff[2]*amp, 0.0f, 1.0f) * 255);
                        }
                    }
                } else if (img.loaded && !img.pixels.empty()) {
                    // 일반 모드: 이미지 픽셀 읽기
                    int off = (py * img.width + px) * 4;
                    r = img.pixels[off];
                    g = img.pixels[off + 1];
                    b = img.pixels[off + 2];
                }
            }

            // 채널 모드 필터 적용 (shader u_channel 로직과 동일)
            if (state.channel_mode == ChannelMode::Red)   { g = r; b = r; }
            else if (state.channel_mode == ChannelMode::Green) { r = g; b = g; }
            else if (state.channel_mode == ChannelMode::Blue)  { r = b; g = b; }

            float rx = tx + (dx + radius) * cell_size;
            float ry = ty + (dy + radius) * cell_size;
            dl->AddRectFilled(ImVec2(rx, ry),
                              ImVec2(rx + cell_size, ry + cell_size),
                              IM_COL32(r, g, b, 255));
        }
    }

    // 그리드 라인
    ImU32 grid_col = IM_COL32(80, 80, 80, 120);
    for (int i = 0; i <= grid_size; ++i) {
        float x = tx + i * cell_size;
        float y = ty + i * cell_size;
        dl->AddLine(ImVec2(x, ty), ImVec2(x, ty + tooltip_size), grid_col, 1.0f);
        dl->AddLine(ImVec2(tx, y), ImVec2(tx + tooltip_size, y), grid_col, 1.0f);
    }

    // 중심 픽셀 노란색 테두리
    float cx = tx + radius * cell_size;
    float cy = ty + radius * cell_size;
    dl->AddRect(ImVec2(cx, cy), ImVec2(cx + cell_size, cy + cell_size),
                IM_COL32(255, 255, 0, 255), 0.0f, 0, 2.0f);

    // 좌표 라벨
    char label[48];
    std::snprintf(label, sizeof(label), "(%d, %d)", center_x, center_y);
    ImVec2 label_sz = ImGui::CalcTextSize(label);
    float lx = tx + (tooltip_size - label_sz.x) * 0.5f;
    float ly = ty + tooltip_size + 4.0f;
    dl->AddRectFilled(ImVec2(lx - 3, ly - 1),
                      ImVec2(lx + label_sz.x + 3, ly + label_sz.y + 1),
                      IM_COL32(0, 0, 0, 200), 3.0f);
    dl->AddText(ImVec2(lx, ly), IM_COL32(255, 255, 0, 240), label);

    // 모든 Diff 모드: 중심 픽셀의 diff값 표시
    if (is_diff_panel && state.diff.mode != DiffState::Mode::None &&
        imgA.loaded && imgB.loaded &&
        center_x >= 0 && center_x < imgA.width && center_x < imgB.width &&
        center_y >= 0 && center_y < imgA.height && center_y < imgB.height) {
        char sr[16], sg[16], sb[16];
        bool has_diff = false;

        if (imgA.ppm_maxval > 0 && !imgA.pixels_orig.empty() &&
            imgB.ppm_maxval > 0 && !imgB.pixels_orig.empty()) {
            int oa = (center_y * imgA.width + center_x) * 3;
            int ob = (center_y * imgB.width + center_x) * 3;
            fmt_int_pixel(sr, sizeof(sr),
                std::abs((int)imgA.pixels_orig[oa + 0] - (int)imgB.pixels_orig[ob + 0]), state.pixel_format);
            fmt_int_pixel(sg, sizeof(sg),
                std::abs((int)imgA.pixels_orig[oa + 1] - (int)imgB.pixels_orig[ob + 1]), state.pixel_format);
            fmt_int_pixel(sb, sizeof(sb),
                std::abs((int)imgA.pixels_orig[oa + 2] - (int)imgB.pixels_orig[ob + 2]), state.pixel_format);
            has_diff = true;
        } else if (!imgA.pixels.empty() && !imgB.pixels.empty()) {
            int off_a = (center_y * imgA.width + center_x) * 4;
            int off_b = (center_y * imgB.width + center_x) * 4;
            fmt_int_pixel(sr, sizeof(sr),
                std::abs((int)imgA.pixels[off_a + 0] - (int)imgB.pixels[off_b + 0]), state.pixel_format);
            fmt_int_pixel(sg, sizeof(sg),
                std::abs((int)imgA.pixels[off_a + 1] - (int)imgB.pixels[off_b + 1]), state.pixel_format);
            fmt_int_pixel(sb, sizeof(sb),
                std::abs((int)imgA.pixels[off_a + 2] - (int)imgB.pixels[off_b + 2]), state.pixel_format);
            has_diff = true;
        }

        if (has_diff) {
            char diff_label[64];
            ImU32 diff_col;
            if (state.channel_mode == ChannelMode::Red) {
                std::snprintf(diff_label, sizeof(diff_label), "diff(R): %s", sr);
                diff_col = IM_COL32(255, 100, 100, 240);
            } else if (state.channel_mode == ChannelMode::Green) {
                std::snprintf(diff_label, sizeof(diff_label), "diff(G): %s", sg);
                diff_col = IM_COL32(100, 255, 100, 240);
            } else if (state.channel_mode == ChannelMode::Blue) {
                std::snprintf(diff_label, sizeof(diff_label), "diff(B): %s", sb);
                diff_col = IM_COL32(100, 130, 255, 240);
            } else {
                std::snprintf(diff_label, sizeof(diff_label), "diff: %s, %s, %s", sr, sg, sb);
                diff_col = IM_COL32(0, 255, 255, 240);
            }
            ImVec2 dl_sz = ImGui::CalcTextSize(diff_label);
            float dlx = tx + (tooltip_size - dl_sz.x) * 0.5f;
            float dly = ly + label_sz.y + 4.0f;
            dl->AddRectFilled(ImVec2(dlx - 3, dly - 1),
                              ImVec2(dlx + dl_sz.x + 3, dly + dl_sz.y + 1),
                              IM_COL32(0, 0, 0, 200), 3.0f);
            dl->AddText(ImVec2(dlx, dly), diff_col, diff_label);
        }
    }

    // A/B 패널: 중심 픽셀의 RGB 값 표시
    if (!is_diff_panel && img.loaded &&
        center_x >= 0 && center_x < img.width &&
        center_y >= 0 && center_y < img.height) {
        char sr[16], sg[16], sb[16];
        bool has_val = false;

        if (img.ppm_maxval > 0 && !img.pixels_orig.empty()) {
            int oidx = (center_y * img.width + center_x) * 3;
            fmt_int_pixel(sr, sizeof(sr), (int)img.pixels_orig[oidx + 0], state.pixel_format);
            fmt_int_pixel(sg, sizeof(sg), (int)img.pixels_orig[oidx + 1], state.pixel_format);
            fmt_int_pixel(sb, sizeof(sb), (int)img.pixels_orig[oidx + 2], state.pixel_format);
            has_val = true;
        } else if (!img.pixels.empty()) {
            int off = (center_y * img.width + center_x) * 4;
            fmt_int_pixel(sr, sizeof(sr), img.pixels[off + 0], state.pixel_format);
            fmt_int_pixel(sg, sizeof(sg), img.pixels[off + 1], state.pixel_format);
            fmt_int_pixel(sb, sizeof(sb), img.pixels[off + 2], state.pixel_format);
            has_val = true;
        }

        if (has_val) {
            char rgb_label[64];
            ImU32 rgb_col;
            if (state.channel_mode == ChannelMode::Red) {
                std::snprintf(rgb_label, sizeof(rgb_label), "R: %s", sr);
                rgb_col = IM_COL32(255, 100, 100, 240);
            } else if (state.channel_mode == ChannelMode::Green) {
                std::snprintf(rgb_label, sizeof(rgb_label), "G: %s", sg);
                rgb_col = IM_COL32(100, 255, 100, 240);
            } else if (state.channel_mode == ChannelMode::Blue) {
                std::snprintf(rgb_label, sizeof(rgb_label), "B: %s", sb);
                rgb_col = IM_COL32(100, 130, 255, 240);
            } else {
                std::snprintf(rgb_label, sizeof(rgb_label), "RGB: %s, %s, %s", sr, sg, sb);
                rgb_col = IM_COL32(200, 220, 255, 240);
            }
            ImVec2 rl_sz = ImGui::CalcTextSize(rgb_label);
            float rlx = tx + (tooltip_size - rl_sz.x) * 0.5f;
            float rly = ly + label_sz.y + 4.0f;
            dl->AddRectFilled(ImVec2(rlx - 3, rly - 1),
                              ImVec2(rlx + rl_sz.x + 3, rly + rl_sz.y + 1),
                              IM_COL32(0, 0, 0, 200), 3.0f);
            dl->AddText(ImVec2(rlx, rly), rgb_col, rgb_label);
        }
    }
}

// ─── render_pathfinder ────────────────────────────────────────────────────────
// 왼쪽 패널 하단에 미니맵 + 현재 뷰포트 인디케이터를 표시.
// vp.fit 상태이거나 panel_idx != 0이면 표시하지 않음.

void ImagePanel::render_pathfinder(const AppState& state, int panel_idx,
                                    ImVec2 widget_pos, int view_w, int view_h) {
    if (state.pathfinder_mode == 0) return;
    if (panel_idx != 0) return;

    int actual_idx = state.swap_images ? 1 : 0;
    const ImageEntry&    img = state.images[actual_idx];
    const ViewportState& vp  = state.views[panel_idx];

    if (!img.loaded || img.texture_id == 0) return;
    if (vp.fit) return;

    // Minimap size: max 150px wide, proportional height, max 30% panel height
    float mini_w = std::min(150.0f, view_w * 0.2f);
    float aspect = (img.width > 0) ? static_cast<float>(img.height) / img.width : 1.0f;
    float mini_h = mini_w * aspect;
    float max_h  = view_h * 0.3f;
    if (mini_h > max_h) {
        mini_h = max_h;
        mini_w = (aspect > 0.0f) ? mini_h / aspect : mini_h;
    }

    // Position: bottom-left, 8px margin
    constexpr float MARGIN = 8.0f;
    float mx0 = widget_pos.x + MARGIN;
    float my0 = widget_pos.y + view_h - mini_h - MARGIN;
    float mx1 = mx0 + mini_w;
    float my1 = my0 + mini_h;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 두 모드 모두 불투명 gray(32) 배경으로 잔상 방지
    dl->AddRectFilled(ImVec2(mx0, my0), ImVec2(mx1, my1),
                      IM_COL32(32, 32, 32, 255), 4.0f);
    if (state.pathfinder_mode == 1) {
        // Image 모드: gray 배경 위에 이미지 썸네일 추가
        ImTextureID tex_id = static_cast<ImTextureID>(img.texture_id);
        dl->AddImage(tex_id, ImVec2(mx0, my0), ImVec2(mx1, my1));
    }

    // Viewport indicator: compute normalised [0,1] visible image region
    float half_vw = view_w  * 0.5f;
    float half_vh = view_h  * 0.5f;
    float iw      = static_cast<float>(img.width);
    float ih      = static_cast<float>(img.height);
    float half_iw = iw * 0.5f;
    float half_ih = ih * 0.5f;

    // Inverse of shader: img_px = (screen_px - half_view) / zoom - pan + half_img
    float vis_x0 = ((0.0f   - half_vw) / vp.zoom - vp.pan_x + half_iw) / iw;
    float vis_y0 = ((0.0f   - half_vh) / vp.zoom - vp.pan_y + half_ih) / ih;
    float vis_x1 = ((view_w - half_vw) / vp.zoom - vp.pan_x + half_iw) / iw;
    float vis_y1 = ((view_h - half_vh) / vp.zoom - vp.pan_y + half_ih) / ih;

    float cx0 = std::max(0.0f, std::min(1.0f, vis_x0));
    float cy0 = std::max(0.0f, std::min(1.0f, vis_y0));
    float cx1 = std::max(0.0f, std::min(1.0f, vis_x1));
    float cy1 = std::max(0.0f, std::min(1.0f, vis_y1));

    // Map to minimap screen coordinates
    float vx0 = mx0 + cx0 * mini_w;
    float vy0 = my0 + cy0 * mini_h;
    float vx1 = mx0 + cx1 * mini_w;
    float vy1 = my0 + cy1 * mini_h;

    if (vx1 > vx0 + 1.0f && vy1 > vy0 + 1.0f) {
        if (state.pathfinder_mode == 2) {
            // Schematic: 반투명 노란색 채움
            dl->AddRectFilled(ImVec2(vx0, vy0), ImVec2(vx1, vy1),
                              IM_COL32(255, 220, 0, 80));
        }
        // 양쪽 모드 공통: 노란색 외곽선
        dl->AddRect(ImVec2(vx0, vy0), ImVec2(vx1, vy1),
                    IM_COL32(255, 220, 0, 220), 0.0f, 0, 1.5f);
    }

    // Minimap border
    dl->AddRect(ImVec2(mx0, my0), ImVec2(mx1, my1),
                IM_COL32(255, 255, 255, 100), 4.0f, 0, 1.0f);
}

// ─── render_single ────────────────────────────────────────────────────────────

void ImagePanel::render_single(AppState& state, int panel_idx) {
    int actual_idx = state.swap_images ? (1 - panel_idx) : panel_idx;
    const ImageEntry& img = state.images[actual_idx];
    ViewportState&    vp  = state.views[panel_idx];

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int    pw    = std::max(1, static_cast<int>(avail.x));
    int    ph    = std::max(1, static_cast<int>(avail.y));

    if (!img.loaded || img.texture_id == 0) {
        draw_empty_panel_msg(state, actual_idx);
        return;
    }

    if (vp.fit) {
        viewport_fit(vp, img.width, img.height, pw, ph);
    }

    // Clamp pan before shader uses it (ensure shader and border use same values)
    viewport_clamp_pan(vp, img.width, img.height, pw, ph);

    // Ensure FBO matches panel size
    if (!fbo_.ensure(pw, ph)) return;

    // Render image into FBO
    fbo_.bind();
    glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(img.texture_id));
    // Nearest neighbor for ≥1× zoom, linear+mipmap for zoom-out
    if (vp.zoom >= 1.0f) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }

    image_shader_.use();
    image_shader_.set_int  ("u_tex",        0);
    image_shader_.set_vec2 ("u_image_size", static_cast<float>(img.width),
                                            static_cast<float>(img.height));
    image_shader_.set_vec2 ("u_view_size",  static_cast<float>(pw),
                                            static_cast<float>(ph));

    image_shader_.set_float("u_zoom",       vp.zoom);
    image_shader_.set_vec2 ("u_pan",        vp.pan_x, vp.pan_y);
    image_shader_.set_int  ("u_channel",   static_cast<int>(state.channel_mode));

    // 3D LUT / CDL display transform (not on SSIM/FLIP heatmap fake-entries).
    int lut_on = (state.lut_enabled && state.lut_texture_id &&
                  state.diff.mode != DiffState::Mode::SSIM &&
                  state.diff.mode != DiffState::Mode::FLIP) ? 1 : 0;
    // u_lut must ALWAYS name a unit other than u_tex's unit 0: a sampler3D and a
    // sampler2D sharing one texture unit is invalid GL state and draws black on
    // macOS (Metal-backed GL 4.1) even when the LUT branch is never taken.
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, lut_on ? static_cast<GLuint>(state.lut_texture_id) : 0);
    glActiveTexture(GL_TEXTURE0);
    image_shader_.set_int("u_lut", 1);
    image_shader_.set_int("u_lut_enabled", lut_on);

    quad_.draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    fbo_.unbind();

    // Display FBO texture.
    // FBO bottom (GL t=0) = image top (shader maps screen_px.y=0 → img_px.y=0).
    // ImGui top → UV(0,0) → FBO bottom → image top.  No flip needed.
    ImTextureID tex = static_cast<ImTextureID>(fbo_.tex_id);
    ImGui::Image(tex, avail, ImVec2(0, 0), ImVec2(1, 1));
    bool img_hovered = ImGui::IsItemHovered();

    ImVec2 widget_pos = ImGui::GetItemRectMin();
    if (state.roi.active) {
        handle_roi_drag(state, panel_idx, widget_pos, pw, ph,
                        img.width, img.height);
    } else {
        handle_mouse_pan(state, panel_idx);
        handle_mouse_double_click(state, panel_idx, widget_pos, pw, ph,
                                   img.width, img.height);
        handle_mouse_right_select(state, panel_idx, widget_pos, pw, ph,
                                   img.width, img.height, panel_idx);
    }

    if (state.show_borders)
        draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                          static_cast<float>(pw), static_cast<float>(ph),
                          static_cast<float>(img.width), static_cast<float>(img.height),
                          vp.pan_x, vp.pan_y, vp.zoom,
                          static_cast<ImU32>(state.border_colors[panel_idx]),
                          /*active=*/ panel_idx == state.active_panel);

    if (state.roi.has_roi || state.roi.dragging) {
        render_roi_overlay(state, panel_idx, widget_pos, pw, ph,
                           img.width, img.height);
    }

    if (state.comp.active && state.comp.computed) {
        render_comp_overlay(state, panel_idx, widget_pos, pw, ph,
                            img.width, img.height);
    }

    if (vp.zoom >= 32.0f && img.loaded) {
        render_pixel_values(state, panel_idx, widget_pos, pw, ph);
    }

    update_mouse_constraint(state, panel_idx, widget_pos, pw, ph,
                            img.width, img.height, img_hovered);
    render_magnifier(state, panel_idx, widget_pos, pw, ph,
                     img.width, img.height, false, img_hovered);
    render_crosshair(state, panel_idx, widget_pos, pw, ph, img.width, img.height);
    render_bad_pixel_overlay(state, panel_idx, widget_pos, pw, ph, img.width, img.height);

    if (panel_idx == 0) {
        render_pathfinder(state, panel_idx, widget_pos, pw, ph);
    }
}

// ─── render_pixel_values ─────────────────────────────────────────────────

void ImagePanel::render_pixel_values(const AppState& state, int panel_idx,
                                     ImVec2 widget_pos, int view_w, int view_h) {
    int actual_idx = state.swap_images ? (1 - panel_idx) : panel_idx;
    const ImageEntry& img = state.images[actual_idx];
    const ViewportState& vp = state.views[panel_idx];

    if (!img.loaded) return;

    float zoom = vp.zoom;

    // Shader transform: img_px = (screen_px - view_size*0.5) / zoom - u_pan + image_size*0.5
    // Inverse:          screen_px = (img_px + u_pan - image_size*0.5) * zoom + view_size*0.5
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img.width * 0.5f;
    float half_ih = img.height * 0.5f;

    // screen_px = 0 -> img_px, screen_px = view_w -> img_px
    float img_x0 = (0.0f - half_vw) / zoom - vp.pan_x + half_iw;
    float img_y0 = (0.0f - half_vh) / zoom - vp.pan_y + half_ih;
    float img_x1 = (view_w - half_vw) / zoom - vp.pan_x + half_iw;
    float img_y1 = (view_h - half_vh) / zoom - vp.pan_y + half_ih;

    int px_start_x = std::max(0, (int)std::floor(img_x0));
    int px_start_y = std::max(0, (int)std::floor(img_y0));
    int px_end_x   = std::min(img.width,  (int)std::ceil(img_x1));
    int px_end_y   = std::min(img.height, (int)std::ceil(img_y1));

    if (px_start_x >= px_end_x || px_start_y >= px_end_y) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ── Pixel grid lines ──────────────────────────────────────────────────
    ImU32 grid_col = IM_COL32(255, 255, 255, 50);
    float clip_x0 = widget_pos.x;
    float clip_y0 = widget_pos.y;
    float clip_x1 = widget_pos.x + view_w;
    float clip_y1 = widget_pos.y + view_h;

    // Inverse transform: screen_px = (img_px + pan - half_iw) * zoom + half_vw
    for (int px = px_start_x; px <= px_end_x; ++px) {
        float sx = (px + vp.pan_x - half_iw) * zoom + half_vw + widget_pos.x;
        if (sx >= clip_x0 && sx <= clip_x1)
            dl->AddLine(ImVec2(sx, clip_y0), ImVec2(sx, clip_y1), grid_col);
    }
    for (int py = px_start_y; py <= px_end_y; ++py) {
        float sy = (py + vp.pan_y - half_ih) * zoom + half_vh + widget_pos.y;
        if (sy >= clip_y0 && sy <= clip_y1)
            dl->AddLine(ImVec2(clip_x0, sy), ImVec2(clip_x1, sy), grid_col);
    }

    // ── RGB value text ────────────────────────────────────────────────────
    float font_size = std::min(zoom * 0.25f, 20.0f);
    if (font_size < 6.0f) return;

    float scale = font_size / ImGui::GetFontSize();
    ImU32 col_r  = IM_COL32(255, 100, 100, 230);
    ImU32 col_g  = IM_COL32(100, 255, 100, 230);
    ImU32 col_b  = IM_COL32(100, 130, 255, 230);
    ImU32 shadow = IM_COL32(0, 0, 0, 140);

    for (int py = px_start_y; py < px_end_y; ++py) {
        for (int px = px_start_x; px < px_end_x; ++px) {
            // Pixel center (px+0.5, py+0.5) -> screen position
            float scr_x = (px + 0.5f + vp.pan_x - half_iw) * zoom + half_vw;
            float scr_y = (py + 0.5f + vp.pan_y - half_ih) * zoom + half_vh;
            float abs_x = widget_pos.x + scr_x;
            float abs_y = widget_pos.y + scr_y;

            char sr[16], sg[16], sb[16];

            if (img.ppm_maxval > 0 && !img.pixels_orig.empty()) {
                int oidx = (py * img.width + px) * 3;
                fmt_int_pixel(sr, sizeof(sr), (int)img.pixels_orig[oidx + 0], state.pixel_format);
                fmt_int_pixel(sg, sizeof(sg), (int)img.pixels_orig[oidx + 1], state.pixel_format);
                fmt_int_pixel(sb, sizeof(sb), (int)img.pixels_orig[oidx + 2], state.pixel_format);
            } else if (!img.pixels_f32.empty()) {
                int pidx = (py * img.width + px) * 4;
                std::snprintf(sr, sizeof(sr), "%.2f", img.pixels_f32[pidx + 0]);
                std::snprintf(sg, sizeof(sg), "%.2f", img.pixels_f32[pidx + 1]);
                std::snprintf(sb, sizeof(sb), "%.2f", img.pixels_f32[pidx + 2]);
            } else if (!img.pixels.empty()) {
                int pidx = (py * img.width + px) * 4;
                fmt_int_pixel(sr, sizeof(sr), img.pixels[pidx + 0], state.pixel_format);
                fmt_int_pixel(sg, sizeof(sg), img.pixels[pidx + 1], state.pixel_format);
                fmt_int_pixel(sb, sizeof(sb), img.pixels[pidx + 2], state.pixel_format);
            } else {
                continue;
            }

            if (state.channel_mode == ChannelMode::RGB) {
                ImVec2 szr = ImGui::CalcTextSize(sr);
                ImVec2 szg = ImGui::CalcTextSize(sg);
                ImVec2 szb = ImGui::CalcTextSize(sb);
                szr.x *= scale; szr.y *= scale;
                szg.x *= scale; szg.y *= scale;
                szb.x *= scale; szb.y *= scale;

                float gap = 1.0f * scale;
                float total_h = szr.y + szg.y + szb.y + 2.0f * gap;
                float ry = abs_y - total_h * 0.5f;
                float gy = ry + szr.y + gap;
                float by = gy + szg.y + gap;

                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szr.x * 0.5f + 1, ry + 1), shadow, sr);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szr.x * 0.5f, ry), col_r, sr);

                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szg.x * 0.5f + 1, gy + 1), shadow, sg);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szg.x * 0.5f, gy), col_g, sg);

                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szb.x * 0.5f + 1, by + 1), shadow, sb);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szb.x * 0.5f, by), col_b, sb);
            } else {
                const char* s_ch  = (state.channel_mode == ChannelMode::Red)   ? sr :
                                    (state.channel_mode == ChannelMode::Green)  ? sg : sb;
                ImU32       col_ch = (state.channel_mode == ChannelMode::Red)   ? col_r :
                                    (state.channel_mode == ChannelMode::Green)  ? col_g : col_b;
                ImVec2 sz = ImGui::CalcTextSize(s_ch);
                sz.x *= scale; sz.y *= scale;
                float ty = abs_y - sz.y * 0.5f;
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - sz.x * 0.5f + 1, ty + 1), shadow, s_ch);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - sz.x * 0.5f, ty), col_ch, s_ch);
            }
        }
    }
}

// ─── render_diff ──────────────────────────────────────────────────────────────

void ImagePanel::render_diff(AppState& state, DiffRenderer& diff_renderer) {
    int idx_a = state.swap_images ? 1 : 0;
    int idx_b = state.swap_images ? 0 : 1;

    const ImageEntry& imgA = state.images[idx_a];
    const ImageEntry& imgB = state.images[idx_b];
    ViewportState&    vp   = state.views[0];

    if (!imgA.loaded || !imgB.loaded) {
        ImGui::TextDisabled("(need two images for diff)");
        return;
    }

    // Compute diff pixel list (lazy, cached by image dimensions)
    if (!imgA.pixels.empty() && !imgB.pixels.empty()) {
        compute_diff_pixel_list(imgA, imgB, state.diff_listing);
    }

    // Enhance mode: compute global min/max of non-zero diff
    if (state.diff.mode == DiffState::Mode::Enhance)
        compute_enhance_range(state.diff_listing, state.diff);
    float enh_min_f = state.diff.enhance_max > 0 ? state.diff.enhance_min / 255.0f : 0.0f;
    float enh_max_f = state.diff.enhance_max / 255.0f;

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int    pw    = std::max(1, static_cast<int>(avail.x));
    int    ph    = std::max(1, static_cast<int>(avail.y));

    if (vp.fit) viewport_fit(vp, imgA.width, imgA.height, pw, ph);

    // Clamp pan before shader uses it (ensure shader and border use same values)
    viewport_clamp_pan(vp, imgA.width, imgA.height, pw, ph);

    if (!fbo_.ensure(pw, ph)) return;

    fbo_.bind();
    diff_renderer.render(imgA.texture_id, imgB.texture_id,
                         vp,
                         imgA.width, imgA.height,
                         pw, ph,
                         state.diff.mode, state.diff.amplify,
                         state.channel_mode,
                         state.diff.threshold,
                         enh_min_f, enh_max_f,
                         state.diff.alpha);
    fbo_.unbind();

    // Highlight mode: count non-zero diff pixels (CPU-side, no GPU readback)
    if (state.diff.mode == DiffState::Mode::Highlight &&
        !imgA.pixels.empty() && !imgB.pixels.empty()) {
        auto [nonzero, total] = count_nonzero_diff_pixels(
            imgA, imgB, pw, ph, vp);
        state.diff.threshold_exceed_count = nonzero;
        state.diff.threshold_total_count  = total;
    }

    ImTextureID tex = static_cast<ImTextureID>(fbo_.tex_id);
    ImGui::Image(tex, avail, ImVec2(0, 0), ImVec2(1, 1));
    bool img_hovered = ImGui::IsItemHovered();

    ImVec2 widget_pos = ImGui::GetItemRectMin();
    if (state.roi.active) {
        handle_roi_drag(state, 0, widget_pos, pw, ph,
                        imgA.width, imgA.height);
    } else {
        handle_mouse_pan(state, 0);
        handle_mouse_double_click(state, 0, widget_pos, pw, ph,
                                   imgA.width, imgA.height);
        handle_mouse_right_select(state, 0, widget_pos, pw, ph,
                                   imgA.width, imgA.height, 2);
    }

    if (state.show_borders)
        draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                          static_cast<float>(pw), static_cast<float>(ph),
                          static_cast<float>(imgA.width), static_cast<float>(imgA.height),
                          vp.pan_x, vp.pan_y, vp.zoom,
                          static_cast<ImU32>(state.border_colors[2]));

    if (state.roi.has_roi || state.roi.dragging) {
        render_roi_overlay(state, 0, widget_pos, pw, ph,
                           imgA.width, imgA.height);
    }

    if (vp.zoom >= 32.0f && imgA.loaded && imgB.loaded) {
        render_diff_pixel_values(state, widget_pos, pw, ph);
    }

    // AlphaBlend mode: 비율 HUD (diff 패널 좌상단)
    if (state.diff.mode == DiffState::Mode::AlphaBlend) {
        int pct_b = static_cast<int>(std::round(state.diff.alpha * 100.0f));
        int pct_a = 100 - pct_b;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "A: %d%%  —  B: %d%%", pct_a, pct_b);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        float fs = 20.0f;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, buf);
        float px = widget_pos.x + 12.0f;
        float py = widget_pos.y + 12.0f;
        float pad = 8.0f;
        dl->AddRectFilled(ImVec2(px - pad, py - pad/2),
                          ImVec2(px + ts.x + pad, py + ts.y + pad/2),
                          IM_COL32(15, 20, 30, 210), 4.0f);
        dl->AddRect(ImVec2(px - pad, py - pad/2),
                    ImVec2(px + ts.x + pad, py + ts.y + pad/2),
                    IM_COL32(120, 200, 255, 255), 4.0f, 0, 1.5f);
        dl->AddText(font, fs, ImVec2(px, py), IM_COL32(220, 240, 255, 255), buf);
    }

    // "Identical" overlay when images are the same (channel-aware)
    {
        bool ch_identical = state.diff_listing.identical;
        if (!ch_identical && state.channel_mode != ChannelMode::RGB) {
            if (state.channel_mode == ChannelMode::Red)   ch_identical = state.diff_listing.identical_r;
            if (state.channel_mode == ChannelMode::Green) ch_identical = state.diff_listing.identical_g;
            if (state.channel_mode == ChannelMode::Blue)  ch_identical = state.diff_listing.identical_b;
        }
        if (ch_identical && state.diff.mode != DiffState::Mode::None
                         && state.diff.mode != DiffState::Mode::AlphaBlend) {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            const char* label = "Identical";
            float base_size = ImGui::GetFontSize();
            ImVec2 base_text = ImGui::CalcTextSize(label);
            float target_w = pw * 0.7f;
            float scale = target_w / base_text.x;
            float font_size = std::min(base_size * scale, 200.0f);
            ImVec2 text_sz = ImGui::GetFont()->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label);
            float tx = widget_pos.x + (pw - text_sz.x) * 0.5f;
            float ty = widget_pos.y + (ph - text_sz.y) * 0.5f;
            dl->AddText(ImGui::GetFont(), font_size, ImVec2(tx+2, ty+2), IM_COL32(0,0,0,160), label);
            dl->AddText(ImGui::GetFont(), font_size, ImVec2(tx, ty), IM_COL32(0,255,120,220), label);
        }
    }

    update_mouse_constraint(state, 0, widget_pos, pw, ph,
                            imgA.width, imgA.height, img_hovered);
    render_magnifier(state, 0, widget_pos, pw, ph,
                     imgA.width, imgA.height, true, img_hovered);
    render_crosshair(state, 0, widget_pos, pw, ph, imgA.width, imgA.height, true);
}

// ─── render_diff_pixel_values ─────────────────────────────────────────────

void ImagePanel::render_diff_pixel_values(const AppState& state,
                                          ImVec2 widget_pos, int view_w, int view_h) {
    int idx_a = state.swap_images ? 1 : 0;
    int idx_b = state.swap_images ? 0 : 1;
    const ImageEntry& imgA = state.images[idx_a];
    const ImageEntry& imgB = state.images[idx_b];
    const ViewportState& vp = state.views[0];

    if (!imgA.loaded || !imgB.loaded) return;

    float zoom = vp.zoom;

    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = imgA.width * 0.5f;
    float half_ih = imgA.height * 0.5f;

    float img_x0 = (0.0f - half_vw) / zoom - vp.pan_x + half_iw;
    float img_y0 = (0.0f - half_vh) / zoom - vp.pan_y + half_ih;
    float img_x1 = (view_w - half_vw) / zoom - vp.pan_x + half_iw;
    float img_y1 = (view_h - half_vh) / zoom - vp.pan_y + half_ih;

    int px_start_x = std::max(0, (int)std::floor(img_x0));
    int px_start_y = std::max(0, (int)std::floor(img_y0));
    int px_end_x   = std::min(imgA.width,  (int)std::ceil(img_x1));
    int px_end_y   = std::min(imgA.height, (int)std::ceil(img_y1));

    if (px_start_x >= px_end_x || px_start_y >= px_end_y) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // ── Pixel grid lines ──────────────────────────────────────────────────
    ImU32 grid_col = IM_COL32(255, 255, 255, 50);
    float clip_x0 = widget_pos.x;
    float clip_y0 = widget_pos.y;
    float clip_x1 = widget_pos.x + view_w;
    float clip_y1 = widget_pos.y + view_h;

    for (int px = px_start_x; px <= px_end_x; ++px) {
        float sx = (px + vp.pan_x - half_iw) * zoom + half_vw + widget_pos.x;
        if (sx >= clip_x0 && sx <= clip_x1)
            dl->AddLine(ImVec2(sx, clip_y0), ImVec2(sx, clip_y1), grid_col);
    }
    for (int py = px_start_y; py <= px_end_y; ++py) {
        float sy = (py + vp.pan_y - half_ih) * zoom + half_vh + widget_pos.y;
        if (sy >= clip_y0 && sy <= clip_y1)
            dl->AddLine(ImVec2(clip_x0, sy), ImVec2(clip_x1, sy), grid_col);
    }

    // ── Diff value text ────────────────────────────────────────────────────
    float font_size = std::min(zoom * 0.25f, 20.0f);
    if (font_size < 6.0f) return;

    float scale = font_size / ImGui::GetFontSize();
    ImU32 col_r  = IM_COL32(255, 100, 100, 230);
    ImU32 col_g  = IM_COL32(100, 255, 100, 230);
    ImU32 col_b  = IM_COL32(100, 130, 255, 230);
    ImU32 shadow = IM_COL32(0, 0, 0, 140);

    bool has_orig = imgA.ppm_maxval > 0 && !imgA.pixels_orig.empty() &&
                    imgB.ppm_maxval > 0 && !imgB.pixels_orig.empty();
    bool has_f32 = !imgA.pixels_f32.empty() && !imgB.pixels_f32.empty();
    bool is_hdr  = imgA.is_hdr && imgB.is_hdr;
    bool has_u8  = !imgA.pixels.empty() && !imgB.pixels.empty();

    for (int py = px_start_y; py < px_end_y; ++py) {
        for (int px = px_start_x; px < px_end_x; ++px) {
            float scr_x = (px + 0.5f + vp.pan_x - half_iw) * zoom + half_vw;
            float scr_y = (py + 0.5f + vp.pan_y - half_ih) * zoom + half_vh;
            float abs_x = widget_pos.x + scr_x;
            float abs_y = widget_pos.y + scr_y;

            int pidx_a = (py * imgA.width + px) * 4;
            int pidx_b = (py * imgB.width + px) * 4;
            char sr[16], sg[16], sb[16];

            if (has_orig) {
                int oidx_a = (py * imgA.width + px) * 3;
                int oidx_b = (py * imgB.width + px) * 3;
                fmt_int_pixel(sr, sizeof(sr),
                    std::abs((int)imgA.pixels_orig[oidx_a + 0] - (int)imgB.pixels_orig[oidx_b + 0]), state.pixel_format);
                fmt_int_pixel(sg, sizeof(sg),
                    std::abs((int)imgA.pixels_orig[oidx_a + 1] - (int)imgB.pixels_orig[oidx_b + 1]), state.pixel_format);
                fmt_int_pixel(sb, sizeof(sb),
                    std::abs((int)imgA.pixels_orig[oidx_a + 2] - (int)imgB.pixels_orig[oidx_b + 2]), state.pixel_format);
            } else if (is_hdr && has_f32) {
                std::snprintf(sr, sizeof(sr), "%.2f",
                    std::fabs(imgA.pixels_f32[pidx_a + 0] - imgB.pixels_f32[pidx_b + 0]));
                std::snprintf(sg, sizeof(sg), "%.2f",
                    std::fabs(imgA.pixels_f32[pidx_a + 1] - imgB.pixels_f32[pidx_b + 1]));
                std::snprintf(sb, sizeof(sb), "%.2f",
                    std::fabs(imgA.pixels_f32[pidx_a + 2] - imgB.pixels_f32[pidx_b + 2]));
            } else if (has_u8) {
                fmt_int_pixel(sr, sizeof(sr),
                    std::abs((int)imgA.pixels[pidx_a + 0] - (int)imgB.pixels[pidx_b + 0]), state.pixel_format);
                fmt_int_pixel(sg, sizeof(sg),
                    std::abs((int)imgA.pixels[pidx_a + 1] - (int)imgB.pixels[pidx_b + 1]), state.pixel_format);
                fmt_int_pixel(sb, sizeof(sb),
                    std::abs((int)imgA.pixels[pidx_a + 2] - (int)imgB.pixels[pidx_b + 2]), state.pixel_format);
            } else {
                continue;
            }

            if (state.channel_mode == ChannelMode::RGB) {
                ImVec2 szr = ImGui::CalcTextSize(sr);
                ImVec2 szg = ImGui::CalcTextSize(sg);
                ImVec2 szb = ImGui::CalcTextSize(sb);
                szr.x *= scale; szr.y *= scale;
                szg.x *= scale; szg.y *= scale;
                szb.x *= scale; szb.y *= scale;

                float gap = 1.0f * scale;
                float total_h = szr.y + szg.y + szb.y + 2.0f * gap;
                float ry = abs_y - total_h * 0.5f;
                float gy = ry + szr.y + gap;
                float by = gy + szg.y + gap;

                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szr.x * 0.5f + 1, ry + 1), shadow, sr);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szr.x * 0.5f, ry), col_r, sr);

                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szg.x * 0.5f + 1, gy + 1), shadow, sg);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szg.x * 0.5f, gy), col_g, sg);

                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szb.x * 0.5f + 1, by + 1), shadow, sb);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szb.x * 0.5f, by), col_b, sb);
            } else {
                const char* s_ch  = (state.channel_mode == ChannelMode::Red)   ? sr :
                                    (state.channel_mode == ChannelMode::Green)  ? sg : sb;
                ImU32       col_ch = (state.channel_mode == ChannelMode::Red)   ? col_r :
                                    (state.channel_mode == ChannelMode::Green)  ? col_g : col_b;
                ImVec2 sz = ImGui::CalcTextSize(s_ch);
                sz.x *= scale; sz.y *= scale;
                float ty = abs_y - sz.y * 0.5f;
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - sz.x * 0.5f + 1, ty + 1), shadow, s_ch);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - sz.x * 0.5f, ty), col_ch, s_ch);
            }
        }
    }
}

// ─── render ───────────────────────────────────────────────────────────────────

void ImagePanel::render(AppState& state, int panel_idx, DiffRenderer& diff_renderer,
                        bool force_single) {
    if (!inited_) return;

    // Software mode: full feature rendering via CPU
    if (is_software_mode()) {
        if (!force_single) {
            bool both_loaded = state.images[0].loaded && state.images[1].loaded;
            // Overlay mode
            if (state.overlay.active && panel_idx == 0 && both_loaded) {
                render_overlay_software(state);
                return;
            }
            // Diff modes (abs/rel/falsecolor) — SSIM/FLIP are precomputed heatmaps
            bool is_diff_mode = (state.diff.mode != DiffState::Mode::None &&
                                 state.diff.mode != DiffState::Mode::SSIM &&
                                 state.diff.mode != DiffState::Mode::FLIP);
            if (is_diff_mode && panel_idx == 0) {
                render_diff_software(state);
                return;
            }
            // SSIM / FLIP heatmap
            bool heat_mode = (state.diff.mode == DiffState::Mode::SSIM ||
                              state.diff.mode == DiffState::Mode::FLIP);
            uintptr_t heat_tex = (state.diff.mode == DiffState::Mode::FLIP)
                               ? state.diff.flip_texture_id : state.diff.ssim_texture_id;
            if (heat_mode && panel_idx == 0 && heat_tex != 0) {
                render_heatmap_software(state);
                return;
            }
        }
        render_single_software(state, panel_idx);
        return;
    }

    if (!force_single) {
        // Overlay 모드: panel_idx==0에서만 blend/curtain 렌더링
        if (state.overlay.active && panel_idx == 0 &&
            state.images[0].loaded && state.images[1].loaded) {
            render_overlay(state, diff_renderer);
            return;
        }

        // Diff modes take over panel 0 and show both images
        // (SSIM/FLIP are precomputed heatmaps, handled separately below)
        bool is_diff_mode = (state.diff.mode != DiffState::Mode::None &&
                             state.diff.mode != DiffState::Mode::SSIM &&
                             state.diff.mode != DiffState::Mode::FLIP);

        if (is_diff_mode && panel_idx == 0) {
            render_diff(state, diff_renderer);
            return;
        }

        // SSIM / FLIP heatmap: display precomputed texture in panel 0
        bool heat_mode = (state.diff.mode == DiffState::Mode::SSIM ||
                          state.diff.mode == DiffState::Mode::FLIP);
        uintptr_t heat_tex = (state.diff.mode == DiffState::Mode::FLIP)
                           ? state.diff.flip_texture_id : state.diff.ssim_texture_id;
        if (heat_mode && panel_idx == 0 && heat_tex != 0) {
            ImageEntry fake{};
            fake.loaded     = true;
            fake.texture_id = heat_tex;
            fake.width      = state.images[0].width;
            fake.height     = state.images[0].height;
            auto saved = state.images[0];
            state.images[0] = fake;
            render_single(state, 0);
            state.images[0] = saved;
            return;
        }
    }

    render_single(state, panel_idx);
}

// ─── ROI drag selection ────────────────────────────────────────────────────────

void ImagePanel::handle_roi_drag(AppState& state, int panel_idx,
                                  ImVec2 widget_pos, int view_w, int view_h,
                                  int img_w, int img_h)
{
    auto& roi = state.roi;

    // Scroll wheel still zooms in ROI mode
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            auto& v = state.views[panel_idx];
            if (wheel > 0) viewport_zoom_in(v);
            else           viewport_zoom_out(v);
            if (state.sync_viewports) {
                auto& ov = state.views[1 - panel_idx];
                ov.zoom = v.zoom;
                ov.fit  = false;
            }
        }
    }

    // 드래그 시작
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        roi.dragging   = true;
        roi.drag_panel = panel_idx;
        ImVec2 mp = ImGui::GetMousePos();
        roi.drag_sx = mp.x;
        roi.drag_sy = mp.y;
    }

    if (!roi.dragging || roi.drag_panel != panel_idx) return;

    ImVec2 curr = ImGui::GetMousePos();
    float rx0 = std::min(roi.drag_sx, curr.x);
    float ry0 = std::min(roi.drag_sy, curr.y);
    float rx1 = std::max(roi.drag_sx, curr.x);
    float ry1 = std::max(roi.drag_sy, curr.y);

    // 드래그 중 사각형 표시
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(rx0, ry0), ImVec2(rx1, ry1),
                      IM_COL32(100, 200, 255, 30));
    dl->AddRect(ImVec2(rx0, ry0), ImVec2(rx1, ry1),
                IM_COL32(100, 200, 255, 220), 0.0f, 0, 1.5f);

    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) return;

    roi.dragging = false;

    // 너무 작은 드래그 무시
    if ((rx1 - rx0) < 3.0f || (ry1 - ry0) < 3.0f) return;

    // 화면 좌표 → 이미지 픽셀 좌표 변환
    const ViewportState& vp = state.views[panel_idx];
    float zoom    = vp.zoom;
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img_w  * 0.5f;
    float half_ih = img_h  * 0.5f;

    auto s2ix = [&](float sx) {
        return (sx - widget_pos.x - half_vw) / zoom - vp.pan_x + half_iw;
    };
    auto s2iy = [&](float sy) {
        return (sy - widget_pos.y - half_vh) / zoom - vp.pan_y + half_ih;
    };

    float ix0 = s2ix(rx0), iy0 = s2iy(ry0);
    float ix1 = s2ix(rx1), iy1 = s2iy(ry1);

    roi.x = std::max(0, (int)std::floor(ix0));
    roi.y = std::max(0, (int)std::floor(iy0));
    roi.w = std::min((int)std::ceil(ix1), img_w) - roi.x;
    roi.h = std::min((int)std::ceil(iy1), img_h) - roi.y;

    if (roi.w > 0 && roi.h > 0) {
        roi.has_roi   = true;
        roi.panel_idx = panel_idx;
        state.show_roi_stats = true;
    }
}

// ─── ROI overlay drawing ──────────────────────────────────────────────────────

void ImagePanel::render_roi_overlay(const AppState& state, int panel_idx,
                                     ImVec2 widget_pos, int view_w, int view_h,
                                     int img_w, int img_h)
{
    const auto& roi = state.roi;
    if (!roi.has_roi && !roi.dragging) return;

    const ViewportState& vp = state.views[panel_idx];
    float zoom    = vp.zoom;
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img_w  * 0.5f;
    float half_ih = img_h  * 0.5f;

    // 이미지 픽셀 → 화면 좌표
    auto ix2s = [&](float ix) {
        return widget_pos.x + (ix + vp.pan_x - half_iw) * zoom + half_vw;
    };
    auto iy2s = [&](float iy) {
        return widget_pos.y + (iy + vp.pan_y - half_ih) * zoom + half_vh;
    };

    if (roi.has_roi) {
        float sx0 = ix2s(roi.x);
        float sy0 = iy2s(roi.y);
        float sx1 = ix2s(roi.x + roi.w);
        float sy1 = iy2s(roi.y + roi.h);

        // Clip to widget bounds
        float wx0 = widget_pos.x, wy0 = widget_pos.y;
        float wx1 = wx0 + view_w,  wy1 = wy0 + view_h;
        float cx0 = std::max(sx0, wx0), cy0 = std::max(sy0, wy0);
        float cx1 = std::min(sx1, wx1), cy1 = std::min(sy1, wy1);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (cx1 > cx0 && cy1 > cy0) {
            dl->AddRectFilled(ImVec2(cx0, cy0), ImVec2(cx1, cy1),
                              IM_COL32(100, 200, 255, 25));
        }
        dl->AddRect(ImVec2(sx0, sy0), ImVec2(sx1, sy1),
                    IM_COL32(100, 200, 255, 220), 0.0f, 0, 1.5f);

        // ROI 크기 레이블 표시
        char label[64];
        std::snprintf(label, sizeof(label), "%d x %d", roi.w, roi.h);
        dl->AddText(ImVec2(sx0 + 3, sy0 + 3), IM_COL32(100, 200, 255, 220), label);
    }
}

// ─── Comp worst-block overlay (--comp) ────────────────────────────────────────
// img2/img3 패널 위에 원본 대비 PSNR 최악 블록 num_blk개를 순위 색(빨강=최악 →
// 노랑) 테두리로 표시하고, 마우스가 블록 위에 있으면 상세 풍선말을 띄운다.
// render_slot: 0=worst2(img2 패널), 1=worst3(img3 패널; 이때 images[1]에 img_c가
// swap되어 들어와 있으므로 원본=images[0], 비교=images[1]로 균일하게 샘플한다.

void ImagePanel::render_comp_overlay(AppState& state, int panel_idx,
                                     ImVec2 widget_pos, int view_w, int view_h,
                                     int img_w, int img_h)
{
    CompState& cs = state.comp;

    const ViewportState& vp = state.views[panel_idx];
    float zoom    = vp.zoom;
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img_w  * 0.5f;
    float half_ih = img_h  * 0.5f;
    auto ix2s = [&](float ix) {
        return widget_pos.x + (ix + vp.pan_x - half_iw) * zoom + half_vw;
    };
    auto iy2s = [&](float iy) {
        return widget_pos.y + (iy + vp.pan_y - half_ih) * zoom + half_vh;
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ── Ctrl/Cmd+B: 블록 그리드 바운더리 (3패널 공통) ─────────────────────────
    if (cs.show_grid && cs.blk_w * zoom >= 4.0f && cs.blk_h * zoom >= 4.0f) {
        ImU32 grid_col = IM_COL32(120, 220, 255, 110);
        float wx0 = widget_pos.x,          wy0 = widget_pos.y;
        float wx1 = wx0 + view_w,          wy1 = wy0 + view_h;
        float gx0 = std::max(ix2s(0.0f), wx0);
        float gx1 = std::min(ix2s(static_cast<float>(img_w)), wx1);
        float gy0 = std::max(iy2s(0.0f), wy0);
        float gy1 = std::min(iy2s(static_cast<float>(img_h)), wy1);
        int nbx = (img_w + cs.blk_w - 1) / cs.blk_w;
        int nby = (img_h + cs.blk_h - 1) / cs.blk_h;
        for (int bx = 0; bx <= nbx; ++bx) {
            float sx = ix2s(static_cast<float>(std::min(bx * cs.blk_w, img_w)));
            if (sx >= wx0 && sx <= wx1 && gy1 > gy0)
                dl->AddLine(ImVec2(sx, gy0), ImVec2(sx, gy1), grid_col);
        }
        for (int by = 0; by <= nby; ++by) {
            float sy = iy2s(static_cast<float>(std::min(by * cs.blk_h, img_h)));
            if (sy >= wy0 && sy <= wy1 && gx1 > gx0)
                dl->AddLine(ImVec2(gx0, sy), ImVec2(gx1, sy), grid_col);
        }
    }

    // ── 공용 준비: 그리드 유효성 · peak · 보이는 블록 범위 ────────────────────
    bool grids_ok = cs.nbx > 0 && cs.nby > 0 &&
                    cs.mse2_grid.size() == static_cast<size_t>(cs.nbx) * cs.nby &&
                    cs.mse3_grid.size() == static_cast<size_t>(cs.nbx) * cs.nby;
    const ImageEntry& ref0 = state.images[0];
    const double peak = (ref0.is_hdr && !ref0.pixels_f32.empty()) ? 1.0 : 255.0;
    auto s2ix = [&](float sx) {
        return (sx - widget_pos.x - half_vw) / zoom - vp.pan_x + half_iw;
    };
    auto s2iy = [&](float sy) {
        return (sy - widget_pos.y - half_vh) / zoom - vp.pan_y + half_ih;
    };
    int vbx0 = 0, vbx1 = -1, vby0 = 0, vby1 = -1;
    if (grids_ok) {
        vbx0 = std::max(0, static_cast<int>(std::floor(s2ix(widget_pos.x) / cs.blk_w)));
        vbx1 = std::min(cs.nbx - 1,
                        static_cast<int>(std::floor(s2ix(widget_pos.x + view_w) / cs.blk_w)));
        vby0 = std::max(0, static_cast<int>(std::floor(s2iy(widget_pos.y) / cs.blk_h)));
        vby1 = std::min(cs.nby - 1,
                        static_cast<int>(std::floor(s2iy(widget_pos.y + view_h) / cs.blk_h)));
    }

    // ── Ctrl/Cmd+G: 블록 PSNR 히트맵 (비교 패널 전용) — 노랑→빨강, 50dB↑ 투명.
    //    블록이 3px 미만이면 스트라이드로 묶어 셀 내 최악값을 그린다(fit 줌 대응). ──
    if (cs.show_heatmap && grids_ok && cs.render_slot >= 0 && vbx1 >= vbx0) {
        const std::vector<double>& g = (cs.render_slot == 0) ? cs.mse2_grid : cs.mse3_grid;
        float bpx = cs.blk_w * zoom, bpy = cs.blk_h * zoom;
        int stx = std::max(1, static_cast<int>(std::ceil(3.0f / std::max(bpx, 0.001f))));
        int sty = std::max(1, static_cast<int>(std::ceil(3.0f / std::max(bpy, 0.001f))));
        for (int by = vby0; by <= vby1; by += sty) {
            for (int bx = vbx0; bx <= vbx1; bx += stx) {
                double m = 0.0;
                for (int dy = 0; dy < sty && by + dy <= vby1; ++dy)
                    for (int dx = 0; dx < stx && bx + dx <= vbx1; ++dx)
                        m = std::max(m, g[static_cast<size_t>(by + dy) * cs.nbx + (bx + dx)]);
                if (m <= 0.0) continue;
                double bpsnr = 20.0 * std::log10(peak) - 10.0 * std::log10(m);
                float t = static_cast<float>((50.0 - bpsnr) / 30.0);
                if (t <= 0.0f) continue;
                if (t > 1.0f) t = 1.0f;
                float x0 = ix2s(static_cast<float>(bx * cs.blk_w));
                float y0 = iy2s(static_cast<float>(by * cs.blk_h));
                float x1 = ix2s(static_cast<float>(std::min((bx + stx) * cs.blk_w, img_w)));
                float y1 = iy2s(static_cast<float>(std::min((by + sty) * cs.blk_h, img_h)));
                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
                                  IM_COL32(255, static_cast<int>(230 * (1.0f - t)), 0,
                                           static_cast<int>(35 + 130 * t)));
            }
        }
    }

    // ── Ctrl/Cmd+W: 승패 맵 — 블록별 img2 vs img3 우열 (|Δ|>1dB, 3패널 공통) ──
    if (cs.show_winloss && grids_ok && vbx1 >= vbx0 &&
        cs.blk_w * zoom >= 3.0f && cs.blk_h * zoom >= 3.0f) {
        for (int by = vby0; by <= vby1; ++by) {
            for (int bx = vbx0; bx <= vbx1; ++bx) {
                size_t i = static_cast<size_t>(by) * cs.nbx + bx;
                double m2 = cs.mse2_grid[i], m3 = cs.mse3_grid[i];
                if (m2 <= 0.0 && m3 <= 0.0) continue;
                double d = (m2 <= 0.0) ? 99.0 : (m3 <= 0.0) ? -99.0
                         : 10.0 * std::log10(m3 / m2);   // + → img2 우세
                if (std::fabs(d) < 1.0) continue;         // 동급 블록은 표시 안 함
                float t = static_cast<float>(std::min(std::fabs(d) / 6.0, 1.0));
                int a = static_cast<int>(90 + 150 * t);
                ImU32 col = (d > 0) ? IM_COL32(70, 160, 255, a)    // 파랑 = img2 우세
                                    : IM_COL32(255, 150, 40, a);   // 주황 = img3 우세
                dl->AddRect(ImVec2(ix2s(static_cast<float>(bx * cs.blk_w)),
                                   iy2s(static_cast<float>(by * cs.blk_h))),
                            ImVec2(ix2s(static_cast<float>(std::min(bx * cs.blk_w + cs.blk_w, img_w))),
                                   iy2s(static_cast<float>(std::min(by * cs.blk_h + cs.blk_h, img_h)))),
                            col, 0.0f, 0, 2.0f);
            }
        }
        if (cs.render_slot < 0)
            dl->AddText(ImVec2(widget_pos.x + 6, widget_pos.y + 6),
                        IM_COL32(230, 230, 230, 220),
                        "win/loss: blue=img2 better, orange=img3 better (|d|>1dB)");
    }

    // ── Tab 선택 블록: 3패널 공통 흰 박스 + 순위 라벨 ────────────────────────
    if (cs.sel_slot >= 0) {
        float x0 = ix2s(static_cast<float>(cs.sel_x));
        float y0 = iy2s(static_cast<float>(cs.sel_y));
        float x1 = ix2s(static_cast<float>(cs.sel_x + cs.sel_w));
        float y1 = iy2s(static_cast<float>(cs.sel_y + cs.sel_h));
        dl->AddRect(ImVec2(x0 + 1, y0 + 1), ImVec2(x1 + 1, y1 + 1),
                    IM_COL32(0, 0, 0, 200), 0.0f, 0, 3.0f);        // 그림자
        dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1),
                    IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
        char sl[32];
        std::snprintf(sl, sizeof(sl), "SEL #%d %s", cs.sel_rank,
                      cs.sel_slot == 0 ? "img2" : "img3");
        dl->AddText(ImVec2(x0 + 3, y1 + 3), IM_COL32(255, 255, 255, 230), sl);
    }

    // ── hover echo: 다른 패널에서 hover 중인 블록의 같은 위치를 형광 그린으로
    //    표시 (마우스가 그 블록에 머무는 동안만; 1프레임 지연 스냅샷) ──────────
    if (cs.echo_slot >= 0 && cs.echo_slot != cs.render_slot) {
        float ex0 = ix2s(static_cast<float>(cs.echo_x));
        float ey0 = iy2s(static_cast<float>(cs.echo_y));
        float ex1 = ix2s(static_cast<float>(cs.echo_x + cs.echo_w));
        float ey1 = iy2s(static_cast<float>(cs.echo_y + cs.echo_h));
        // 채움 없이 테두리만 — 원본 픽셀을 가리지 않는다 (사용자 요청)
        dl->AddRect(ImVec2(ex0, ey0), ImVec2(ex1, ey1),
                    IM_COL32(0, 255, 140, 240), 0.0f, 0, 2.5f);

        // 비교 패널(원본 제외)에는 이 패널 자신의 블록 PSNR/MSE 풍선말도 표시
        if (cs.render_slot >= 0 && grids_ok && cs.blk_w > 0 && cs.blk_h > 0) {
            const std::vector<double>& g =
                (cs.render_slot == 0) ? cs.mse2_grid : cs.mse3_grid;
            int ebx = cs.echo_x / cs.blk_w, eby = cs.echo_y / cs.blk_h;
            if (ebx >= 0 && ebx < cs.nbx && eby >= 0 && eby < cs.nby) {
                double m = g[static_cast<size_t>(eby) * cs.nbx + ebx];
                char buf[64];
                if (m > 0.0)
                    std::snprintf(buf, sizeof(buf), "PSNR %.2f dB  MSE %.2f",
                                  20.0 * std::log10(peak) - 10.0 * std::log10(m), m);
                else
                    std::snprintf(buf, sizeof(buf), "PSNR inf (identical)");
                ImVec2 ts = ImGui::CalcTextSize(buf);
                float pad = 5.0f;
                float bxp = ex0, byp = ey0 - ts.y - 2 * pad - 4;   // 기본: 박스 위
                if (byp < widget_pos.y) byp = ey1 + 4;              // 좁으면 아래로
                if (bxp + ts.x + 2 * pad > widget_pos.x + view_w)
                    bxp = widget_pos.x + view_w - ts.x - 2 * pad;
                if (bxp < widget_pos.x) bxp = widget_pos.x;
                dl->AddRectFilled(ImVec2(bxp, byp),
                                  ImVec2(bxp + ts.x + 2 * pad, byp + ts.y + 2 * pad),
                                  IM_COL32(20, 20, 20, 210), 4.0f);
                dl->AddRect(ImVec2(bxp, byp),
                            ImVec2(bxp + ts.x + 2 * pad, byp + ts.y + 2 * pad),
                            IM_COL32(0, 255, 140, 200), 4.0f);
                dl->AddText(ImVec2(bxp + pad, byp + pad),
                            IM_COL32(230, 255, 240, 255), buf);
            }
        }
    }

    // 원본 패널(slot -1)은 그리드/echo까지만 — worst rect/풍선말은 비교 패널 전용.
    // Ctrl/Cmd+B(show_worst)로 worst 사각형 전체를 숨길 수 있다.
    if (cs.render_slot < 0 || !cs.show_worst) return;
    const std::vector<CompBlockInfo>& blocks =
        (cs.render_slot == 0) ? cs.worst2 : cs.worst3;
    if (blocks.empty()) return;
    ImVec2 mouse = ImGui::GetMousePos();
    bool mouse_in_panel =
        mouse.x >= widget_pos.x && mouse.x < widget_pos.x + view_w &&
        mouse.y >= widget_pos.y && mouse.y < widget_pos.y + view_h;
    int hovered = -1;

    int n = static_cast<int>(blocks.size());
    for (int i = 0; i < n; ++i) {
        const CompBlockInfo& b = blocks[i];
        float sx0 = ix2s(static_cast<float>(b.x));
        float sy0 = iy2s(static_cast<float>(b.y));
        float sx1 = ix2s(static_cast<float>(b.x + b.w));
        float sy1 = iy2s(static_cast<float>(b.y + b.h));
        if (sx1 < widget_pos.x || sy1 < widget_pos.y ||
            sx0 > widget_pos.x + view_w || sy0 > widget_pos.y + view_h)
            continue;   // 패널 밖
        // 순위 색: #1(최악)=빨강 → #N=노랑
        float t = (n > 1) ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.0f;
        ImU32 col = IM_COL32(255, static_cast<int>(220.0f * t), 0, 230);
        dl->AddRect(ImVec2(sx0, sy0), ImVec2(sx1, sy1), col, 0.0f, 0, 2.0f);
        if (mouse_in_panel &&
            mouse.x >= sx0 && mouse.x < sx1 && mouse.y >= sy0 && mouse.y < sy1)
            hovered = i;
        if (sx1 - sx0 > 22.0f && sy1 - sy0 > 14.0f) {
            char lbl[16];
            std::snprintf(lbl, sizeof(lbl), "#%d", i + 1);
            dl->AddText(ImVec2(sx0 + 2, sy0 + 1), col, lbl);
        }
    }

    if (hovered < 0) return;

    // ── hover 풍선말: 블록 상세 (원본 vs 이 패널 영상) ────────────────────────
    const CompBlockInfo& b = blocks[hovered];

    // hover echo 기록: 다음 프레임에 나머지 두 패널이 같은 위치에 echo 박스 표시
    cs.hover_slot = cs.render_slot;
    cs.hover_x = b.x; cs.hover_y = b.y;
    cs.hover_w = b.w; cs.hover_h = b.h;
    const ImageEntry& A = state.images[0];
    const ImageEntry& B = state.images[1];
    ImGui::BeginTooltip();
    if (state.font_large) ImGui::PushFont(state.font_large);
    const char* pane = (cs.render_slot == 0) ? "img2(mid)" : "img3(right)";
    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.1f, 1.0f),
                       "Block #%d  %s vs orig", hovered + 1, pane);
    ImGui::Text("grid (%d,%d)   px (%d,%d)-(%d,%d)   %dx%d",
                b.bx, b.by, b.x, b.y,
                b.x + b.w - 1, b.y + b.h - 1, b.w, b.h);
    ImGui::Separator();
    if (b.psnr >= 999.0) ImGui::Text("PSNR: inf");
    else                 ImGui::Text("PSNR: %.2f dB    MSE: %.3f", b.psnr, b.mse);
    // 상대 알고리즘의 같은 블록 성적 병기 (주황=상대 우세, 녹색=이 패널 우세)
    if (grids_ok) {
        const std::vector<double>& og =
            (cs.render_slot == 0) ? cs.mse3_grid : cs.mse2_grid;
        const char* oname = (cs.render_slot == 0) ? "img3(right)" : "img2(mid)";
        double om = og[static_cast<size_t>(b.by) * cs.nbx + b.bx];
        if (om <= 0.0) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                               "%s same block: PSNR inf (identical)", oname);
        } else {
            double opsnr = 20.0 * std::log10(peak) - 10.0 * std::log10(om);
            double dd = opsnr - b.psnr;
            ImVec4 col = (dd > 0) ? ImVec4(1.0f, 0.75f, 0.3f, 1.0f)
                                  : ImVec4(0.5f, 1.0f, 0.6f, 1.0f);
            ImGui::TextColored(col, "%s same block: PSNR %.2f dB (d %+.2f)",
                               oname, opsnr, dd);
        }
    }
    ImGui::Text("MSE R/G/B: %.3f / %.3f / %.3f", b.mse_c[0], b.mse_c[1], b.mse_c[2]);
    int npx = b.w * b.h;
    ImGui::Text("error pixels: %d / %d (%.1f%%)", b.nerr, npx,
                npx > 0 ? 100.0 * b.nerr / npx : 0.0);
    if (A.loaded && B.loaded && b.worst_err > 0.0 &&
        b.worst_x < A.width && b.worst_y < A.height &&
        b.worst_x < B.width && b.worst_y < B.height) {
        size_t p = (static_cast<size_t>(b.worst_y) * A.width + b.worst_x) * 4;
        if (!A.pixels.empty() && !B.pixels.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                "worst px (%d,%d)   |d|max = %d",
                b.worst_x, b.worst_y, static_cast<int>(b.worst_err + 0.5));
            ImGui::Text("  orig   R:%d  G:%d  B:%d",
                        A.pixels[p+0], A.pixels[p+1], A.pixels[p+2]);
            ImGui::Text("  this   R:%d  G:%d  B:%d",
                        B.pixels[p+0], B.pixels[p+1], B.pixels[p+2]);
            ImGui::Text("  delta  R:%+d  G:%+d  B:%+d",
                        static_cast<int>(B.pixels[p+0]) - A.pixels[p+0],
                        static_cast<int>(B.pixels[p+1]) - A.pixels[p+1],
                        static_cast<int>(B.pixels[p+2]) - A.pixels[p+2]);
        } else if (!A.pixels_f32.empty() && !B.pixels_f32.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                "worst px (%d,%d)   |d|max = %.4f",
                b.worst_x, b.worst_y, b.worst_err);
            ImGui::Text("  orig   R:%.3f  G:%.3f  B:%.3f",
                        A.pixels_f32[p+0], A.pixels_f32[p+1], A.pixels_f32[p+2]);
            ImGui::Text("  this   R:%.3f  G:%.3f  B:%.3f",
                        B.pixels_f32[p+0], B.pixels_f32[p+1], B.pixels_f32[p+2]);
            ImGui::Text("  delta  R:%+.3f  G:%+.3f  B:%+.3f",
                        B.pixels_f32[p+0] - A.pixels_f32[p+0],
                        B.pixels_f32[p+1] - A.pixels_f32[p+1],
                        B.pixels_f32[p+2] - A.pixels_f32[p+2]);
        }
    }
    if (state.font_large) ImGui::PopFont();
    ImGui::EndTooltip();
}

// ─── Overlay/Blend render ─────────────────────────────────────────────────────

void ImagePanel::render_overlay(AppState& state, DiffRenderer& /*diff_renderer*/)
{
    int idx_a = state.swap_images ? 1 : 0;
    int idx_b = state.swap_images ? 0 : 1;
    const ImageEntry& imgA = state.images[idx_a];
    const ImageEntry& imgB = state.images[idx_b];
    ViewportState&    vp   = state.views[0];

    if (!imgA.loaded || !imgB.loaded) {
        ImGui::TextDisabled("(overlay: need two images)");
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int pw = std::max(1, (int)avail.x);
    int ph = std::max(1, (int)avail.y);

    if (vp.fit) viewport_fit(vp, imgA.width, imgA.height, pw, ph);
    viewport_clamp_pan(vp, imgA.width, imgA.height, pw, ph);

    if (!fbo_.ensure(pw, ph)) return;

    fbo_.bind();
    glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Bind both textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(imgA.texture_id));
    if (vp.zoom >= 1.0f) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(imgB.texture_id));
    if (vp.zoom >= 1.0f) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }

    int blend_mode = (state.overlay.mode == OverlayState::Mode::Curtain) ? 1 : 0;

    blend_shader_.use();
    blend_shader_.set_int  ("u_texA",        0);
    blend_shader_.set_int  ("u_texB",        1);
    blend_shader_.set_vec2 ("u_image_size",  (float)imgA.width, (float)imgA.height);
    blend_shader_.set_vec2 ("u_view_size",   (float)pw, (float)ph);
    blend_shader_.set_float("u_zoom",        vp.zoom);
    blend_shader_.set_vec2 ("u_pan",         vp.pan_x, vp.pan_y);
    blend_shader_.set_int  ("u_channel",     (int)state.channel_mode);
    blend_shader_.set_int  ("u_blend_mode",  blend_mode);
    blend_shader_.set_float("u_alpha",       state.overlay.alpha);
    blend_shader_.set_float("u_curtain_x",   state.overlay.curtain_x);

    quad_.draw();
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
    fbo_.unbind();

    ImTextureID tex = static_cast<ImTextureID>(fbo_.tex_id);
    ImGui::Image(tex, avail, ImVec2(0,0), ImVec2(1,1));

    ImVec2 widget_pos = ImGui::GetItemRectMin();

    // Curtain 모드: 마우스 드래그로 구분선 조정
    if (state.overlay.mode == OverlayState::Mode::Curtain) {
        float curtain_sx = widget_pos.x + state.overlay.curtain_x * pw;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(curtain_sx, widget_pos.y),
                    ImVec2(curtain_sx, widget_pos.y + ph),
                    IM_COL32(255, 255, 255, 200), 2.0f);

        // 구분선 레이블
        char lbl[16];
        std::snprintf(lbl, sizeof(lbl), "A | B");
        dl->AddText(ImVec2(curtain_sx + 4, widget_pos.y + 4),
                    IM_COL32(255, 255, 255, 200), lbl);

        // 마우스로 커튼 드래그
        if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            float mx = ImGui::GetMousePos().x;
            state.overlay.curtain_x = std::clamp((mx - widget_pos.x) / pw, 0.0f, 1.0f);
        }
    } else {
        // Blend 모드: 마우스 팬
        handle_mouse_pan(state, 0);
        handle_mouse_double_click(state, 0, widget_pos, pw, ph,
                                   imgA.width, imgA.height);
    }

    if (state.roi.active) {
        handle_roi_drag(state, 0, widget_pos, pw, ph,
                        imgA.width, imgA.height);
    } else if (state.overlay.mode != OverlayState::Mode::Curtain) {
        handle_mouse_right_select(state, 0, widget_pos, pw, ph,
                                   imgA.width, imgA.height);
    }

    if (state.show_borders)
        draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                          (float)pw, (float)ph,
                          (float)imgA.width, (float)imgA.height,
                          vp.pan_x, vp.pan_y, vp.zoom,
                          IM_COL32(150, 255, 150, 200));

    if (state.roi.has_roi || state.roi.dragging) {
        render_roi_overlay(state, 0, widget_pos, pw, ph,
                           imgA.width, imgA.height);
    }

    render_pathfinder(state, 0, widget_pos, pw, ph);
}

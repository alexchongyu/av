#pragma once

#include "app.h"
#include <string>
#include <vector>
#include <cstdint>

// Perform the actual save for the given image target.
// Reads state.image_save for format/ppm options.
// Sets state.image_save.status_msg / status_error on completion.
void perform_save(const std::string& path,
                  ImageSaveDialog::Target target,
                  AppState& state);

// Compute diff image (RGBA8) on CPU — mirrors diff fragment shader.
std::vector<uint8_t> compute_diff_cpu(const ImageEntry& imgA,
                                       const ImageEntry& imgB,
                                       const DiffState&  diff);

// Open native save-file dialog for right-click context menu.
void open_context_save_dialog(AppState& state, int target_type);

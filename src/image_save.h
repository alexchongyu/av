#pragma once

#include "app.h"
#include <string>

// Perform the actual save after a file path has been selected.
// Reads state.save_state for format/options and pending_target.
void perform_save(const std::string& path,
                  SaveDialogState::Target target,
                  AppState& state);

// Open the native save file dialog (SDL_ShowSaveFileDialog).
// When the user picks a path, sets state.save_state.saved_path
// and state.save_state.save_pending = true.
// default_filename: optional suggested filename (may be nullptr).
void open_save_file_dialog(AppState& state,
                           SaveDialogState::Target target,
                           const char* default_filename = nullptr);

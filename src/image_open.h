#pragma once

#include "app.h"

// Open the native file open dialog (SDL_ShowOpenFileDialog).
// target: 0=Image A, 1=Image B
// When the user picks a file, sets open_state.opened_path and open_pending=true.
void open_open_file_dialog(AppState& state, int target);

// Render the "Open Images" floating window (triggered by Shift+Cmd+O).
void render_open_images_window(AppState& state);

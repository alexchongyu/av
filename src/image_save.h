#pragma once

#include "app.h"
#include <string>

// Perform the actual save for the given image target.
// Reads state.image_save for format/ppm options.
// Sets state.image_save.status_msg / status_error on completion.
void perform_save(const std::string& path,
                  ImageSaveDialog::Target target,
                  AppState& state);

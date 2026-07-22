#pragma once

#include <string>
#include <vector>

// ─── 3D LUT (.cube) / ASC-CDL (.cdl/.ccc) ─────────────────────────────────────
// A colour transform baked into an N×N×N RGB float grid. Data layout matches an
// OpenGL GL_TEXTURE_3D of internalformat RGB32F: index = ((b*N + g)*N + r)*3,
// so texture(lut, vec3(r,g,b)) samples with s=r (width), t=g, p=b (depth).
struct Lut3D {
    int                N = 0;      // grid size per axis (0 = none)
    std::vector<float> data;       // N*N*N*3 floats
    bool               loaded = false;
    std::string        name;
};

// Parse an Adobe/IRIDAS .cube LUT (LUT_3D_SIZE or LUT_1D_SIZE). Returns false on
// malformed/size-mismatched files.
bool lut_load_cube(const std::string& path, Lut3D& out);

// Parse an ASC-CDL (.cdl/.ccc) Slope/Offset/Power + Saturation and bake it into
// an N×N×N grid (default 33). Returns false if the file has no SOP node.
bool lut_load_cdl(const std::string& path, Lut3D& out, int N = 33);

// Trilinearly sample the LUT; r/g/b are updated in place (clamped to [0,1]).
// No-op if the LUT is not loaded.
void lut_apply(const Lut3D& lut, float& r, float& g, float& b);

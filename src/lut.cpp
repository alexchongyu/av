#include "lut.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
}

bool lut_load_cube(const std::string& path, Lut3D& out) {
    std::ifstream f(path);
    if (!f) return false;

    int n3 = 0, n1 = 0;
    std::vector<float> vals;
    std::string line;
    while (std::getline(f, line)) {
        size_t p = line.find_first_not_of(" \t\r\n");
        if (p == std::string::npos || line[p] == '#') continue;
        std::istringstream ss(line.substr(p));
        std::string tok;
        ss >> tok;
        if (tok == "TITLE" || tok == "DOMAIN_MIN" || tok == "DOMAIN_MAX") continue;
        if (tok == "LUT_3D_SIZE") { ss >> n3; continue; }
        if (tok == "LUT_1D_SIZE") { ss >> n1; continue; }
        // Otherwise a data line: tok is the first float.
        try {
            float r = std::stof(tok), g, b;
            if (!(ss >> g >> b)) continue;
            vals.push_back(r); vals.push_back(g); vals.push_back(b);
        } catch (...) { continue; }
    }

    if (n3 > 0) {
        if ((int)vals.size() != n3 * n3 * n3 * 3) return false;
        out.N = n3; out.data = std::move(vals); out.loaded = true;
        out.name = path;
        return true;
    }
    if (n1 > 0) {
        if ((int)vals.size() != n1 * 3) return false;
        // Bake per-channel 1D curve into a 3D grid.
        out.N = n1;
        out.data.assign((size_t)n1 * n1 * n1 * 3, 0.0f);
        for (int b = 0; b < n1; ++b)
            for (int g = 0; g < n1; ++g)
                for (int r = 0; r < n1; ++r) {
                    size_t idx = (((size_t)b * n1 + g) * n1 + r) * 3;
                    out.data[idx + 0] = vals[r * 3 + 0];
                    out.data[idx + 1] = vals[g * 3 + 1];
                    out.data[idx + 2] = vals[b * 3 + 2];
                }
        out.loaded = true; out.name = path;
        return true;
    }
    return false;
}

namespace {
// Extract the inner text of the first <tag ...>...</tag>.
bool find_tag(const std::string& s, const std::string& tag, std::string& out) {
    size_t a = s.find("<" + tag);
    if (a == std::string::npos) return false;
    a = s.find('>', a);
    if (a == std::string::npos) return false;
    ++a;
    size_t b = s.find("</" + tag, a);
    if (b == std::string::npos) return false;
    out = s.substr(a, b - a);
    return true;
}
}

bool lut_load_cdl(const std::string& path, Lut3D& out, int N) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream buf; buf << f.rdbuf();
    std::string s = buf.str();

    float slope[3] = {1,1,1}, off[3] = {0,0,0}, pw[3] = {1,1,1}, sat = 1.0f;
    std::string t;
    bool have_sop = false;
    if (find_tag(s, "Slope",  t)) { std::sscanf(t.c_str(), "%f %f %f", &slope[0], &slope[1], &slope[2]); have_sop = true; }
    if (find_tag(s, "Offset", t))   std::sscanf(t.c_str(), "%f %f %f", &off[0], &off[1], &off[2]);
    if (find_tag(s, "Power",  t))   std::sscanf(t.c_str(), "%f %f %f", &pw[0], &pw[1], &pw[2]);
    if (find_tag(s, "Saturation", t)) std::sscanf(t.c_str(), "%f", &sat);
    if (!have_sop) return false;

    if (N < 2) N = 33;
    out.N = N;
    out.data.assign((size_t)N * N * N * 3, 0.0f);
    for (int b = 0; b < N; ++b)
        for (int g = 0; g < N; ++g)
            for (int r = 0; r < N; ++r) {
                float in[3] = { r/(float)(N-1), g/(float)(N-1), b/(float)(N-1) };
                float o[3];
                for (int c = 0; c < 3; ++c) {           // ASC SOP: (in*slope+offset)^power
                    float v = in[c] * slope[c] + off[c];
                    if (v < 0.0f) v = 0.0f;
                    o[c] = std::pow(v, pw[c]);
                }
                float luma = 0.2126f*o[0] + 0.7152f*o[1] + 0.0722f*o[2];  // ASC SAT
                for (int c = 0; c < 3; ++c) o[c] = clamp01(luma + sat*(o[c] - luma));
                size_t idx = (((size_t)b * N + g) * N + r) * 3;
                out.data[idx + 0] = o[0]; out.data[idx + 1] = o[1]; out.data[idx + 2] = o[2];
            }
    out.loaded = true; out.name = path;
    return true;
}

void lut_apply(const Lut3D& lut, float& r, float& g, float& b) {
    if (!lut.loaded || lut.N < 2) return;
    const int N = lut.N;
    float fr = clamp01(r) * (N-1), fg = clamp01(g) * (N-1), fb = clamp01(b) * (N-1);
    int r0 = (int)fr, g0 = (int)fg, b0 = (int)fb;
    int r1 = std::min(r0+1, N-1), g1 = std::min(g0+1, N-1), b1 = std::min(b0+1, N-1);
    float dr = fr - r0, dg = fg - g0, db = fb - b0;
    auto at = [&](int rr, int gg, int bb, int c) {
        return lut.data[(((size_t)bb*N + gg)*N + rr)*3 + c];
    };
    float o[3];
    for (int c = 0; c < 3; ++c) {
        float c00 = at(r0,g0,b0,c)*(1-dr) + at(r1,g0,b0,c)*dr;
        float c10 = at(r0,g1,b0,c)*(1-dr) + at(r1,g1,b0,c)*dr;
        float c01 = at(r0,g0,b1,c)*(1-dr) + at(r1,g0,b1,c)*dr;
        float c11 = at(r0,g1,b1,c)*(1-dr) + at(r1,g1,b1,c)*dr;
        float c0 = c00*(1-dg) + c10*dg, c1 = c01*(1-dg) + c11*dg;
        o[c] = c0*(1-db) + c1*db;
    }
    r = o[0]; g = o[1]; b = o[2];
}

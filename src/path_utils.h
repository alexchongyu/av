#pragma once
#include <string>
#include <cstring>
#include <algorithm>

// Cross-platform path separator utilities.
// Handles both '/' (Unix) and '\\' (Windows) separators.

inline std::string::size_type path_last_sep(const std::string& p) {
    auto a = p.rfind('/');
    auto b = p.rfind('\\');
    if (a == std::string::npos) return b;
    if (b == std::string::npos) return a;
    return std::max(a, b);
}

inline const char* path_basename(const char* path) {
    const char* a = std::strrchr(path, '/');
    const char* b = std::strrchr(path, '\\');
    const char* s = (!a) ? b : (!b) ? a : (a > b) ? a : b;
    return s ? s + 1 : path;
}

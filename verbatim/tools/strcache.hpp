#pragma once

namespace strcache {
    // Cache a string. Returns a stable pointer to the cached version.
    // Empty strings not allowed.
    const char *cache_str(const char *str, unsigned len);
}


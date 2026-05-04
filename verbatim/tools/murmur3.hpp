#pragma once

#include <cstddef>
#include <cstdint>

namespace murmur3 {
    uint64_t hash(const void *str, size_t len);
}


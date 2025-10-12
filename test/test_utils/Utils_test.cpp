// Test-specific implementation file that includes only the toHex function
// This avoids pulling in Arduino dependencies from the full Utils.cpp

#include "Utils.h"

namespace mesh {

static const char hex_chars[] = "0123456789ABCDEF";

void Utils::toHex(char* dest, const uint8_t* src, size_t len) {
    while (len > 0) {
        uint8_t b = *src++;
        *dest++ = hex_chars[b >> 4];
        *dest++ = hex_chars[b & 0x0F];
        len--;
    }
    *dest = 0;
}

}

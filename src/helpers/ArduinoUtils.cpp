#include "ArduinoUtils.h"

namespace mesh {

static const char hex_chars[] = "0123456789ABCDEF";

void ArduinoUtils::printHex(Stream& s, const uint8_t* src, size_t len) {
  while (len > 0) {
    uint8_t b = *src++;
    s.print(hex_chars[b >> 4]);
    s.print(hex_chars[b & 0x0F]);
    len--;
  }
}

}

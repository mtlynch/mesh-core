#pragma once

#include <Stream.h>
#include <stddef.h>
#include <stdint.h>

namespace mesh {

class ArduinoUtils {
public:
  /**
   * \brief  Prints the hexadecimal representation of 'src' bytes of given length, to Stream 's'.
  */
  static void printHex(Stream& s, const uint8_t* src, size_t len);
};

}

#pragma once

#include "Arduino.h"
#include "Port.h"

struct Pair {
  Port parentPort;
  Port childPort;
  uint8_t childAngle;
};

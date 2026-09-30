// dht.h (simulation mock) - DHTlib (Rob Tillaart) API for a DHT11.
#pragma once
#include "Arduino.h"
#define DHTLIB_OK 0
#define DHTLIB_ERROR_CHECKSUM -1
#define DHTLIB_ERROR_TIMEOUT -2
#define DHTLIB_INVALID_VALUE -999
class dht {
 public:
  int read11(uint8_t pin);
  double humidity = 0;
  double temperature = 0;
};

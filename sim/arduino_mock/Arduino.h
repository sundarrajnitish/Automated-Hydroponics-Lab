// Arduino.h (simulation mock) - just enough of the Arduino core for the
// firmware in this repo, backed by sim::Board. Behaviour that matters for
// timing (delay, ADC conversion time, serial TX buffering) is modelled.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../vboard.h"

typedef uint8_t byte;
typedef bool boolean;

#define HIGH 0x1
#define LOW 0x0
#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2
#define DEC 10
#define HEX 16

#define A0 54
#define A1 55
#define A2 56
#define A3 57
#define A4 58
#define A5 59

#define F(s) (s)

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);
int analogRead(uint8_t pin);
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);

class Print {
 public:
  virtual size_t write(uint8_t c) { (void)c; return 1; }
  size_t write(const char* s) { size_t n = 0; while (*s) n += write((uint8_t)*s++); return n; }
  size_t print(const char* s) { return write(s); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(unsigned char v, int base = DEC) { return printUnsigned(v, base); }
  size_t print(int v, int base = DEC) { return printSigned(v, base); }
  size_t print(unsigned int v, int base = DEC) { return printUnsigned(v, base); }
  size_t print(long v, int base = DEC) { return printSigned(v, base); }
  size_t print(unsigned long v, int base = DEC) { return printUnsigned(v, base); }
  size_t print(double v, int digits = 2) { return printFloat(v, digits); }
  size_t println() { return write((uint8_t)'\r') + write((uint8_t)'\n'); }
  template <typename T> size_t println(T v) { size_t n = print(v); return n + println(); }
  template <typename T> size_t println(T v, int b) { size_t n = print(v, b); return n + println(); }

 private:
  size_t printUnsigned(unsigned long long v, int base) {
    char buf[24]; int i = 0;
    do { int d = (int)(v % base); buf[i++] = (char)(d < 10 ? '0' + d : 'A' + d - 10); v /= base; } while (v);
    size_t n = 0; while (i) n += write((uint8_t)buf[--i]);
    return n;
  }
  size_t printSigned(long long v, int base) {
    if (v < 0 && base == DEC) { write((uint8_t)'-'); return 1 + printUnsigned((unsigned long long)(-v), base); }
    return printUnsigned((unsigned long long)v, base);
  }
  // Print::printFloat from the Arduino AVR core, same algorithm
  size_t printFloat(double number, int digits) {
    if (number != number) return print("nan");
    if (number > 4294967040.0 || number < -4294967040.0) return print("ovf");
    size_t n = 0;
    if (number < 0.0) { n += print('-'); number = -number; }
    double rounding = 0.5;
    for (int i = 0; i < digits; ++i) rounding /= 10.0;
    number += rounding;
    unsigned long long int_part = (unsigned long long)number;
    double remainder = number - (double)int_part;
    n += printUnsigned(int_part, DEC);
    if (digits > 0) n += print('.');
    while (digits-- > 0) {
      remainder *= 10.0;
      unsigned int d = (unsigned int)remainder;
      n += printUnsigned(d, DEC);
      remainder -= d;
    }
    return n;
  }
};

class HardwareSerial : public Print {
 public:
  void begin(unsigned long baud);
  size_t write(uint8_t c) override;
  using Print::write;
  int available() { return 0; }
  int read() { return -1; }
  void flush();
  explicit operator bool() const { return true; }
};
extern HardwareSerial Serial;

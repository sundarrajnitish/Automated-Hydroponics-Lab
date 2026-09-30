// Minimal runtime for the freestanding WebAssembly build (no libc).
#include <stddef.h>
#include <stdint.h>
extern "C" {
void* memcpy(void* d, const void* s, size_t n) {
  uint8_t* o = (uint8_t*)d; const uint8_t* i = (const uint8_t*)s;
  while (n--) *o++ = *i++;
  return d;
}
void* memmove(void* d, const void* s, size_t n) {
  uint8_t* o = (uint8_t*)d; const uint8_t* i = (const uint8_t*)s;
  if (o < i) { while (n--) *o++ = *i++; }
  else { o += n; i += n; while (n--) *--o = *--i; }
  return d;
}
void* memset(void* d, int c, size_t n) {
  uint8_t* o = (uint8_t*)d;
  while (n--) *o++ = (uint8_t)c;
  return d;
}
int memcmp(const void* a, const void* b, size_t n) {
  const uint8_t* x = (const uint8_t*)a; const uint8_t* y = (const uint8_t*)b;
  for (; n; --n, ++x, ++y) if (*x != *y) return *x - *y;
  return 0;
}
}

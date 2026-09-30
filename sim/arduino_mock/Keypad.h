// Keypad.h (simulation mock) - scheduled key presses come from sim::Board.
#pragma once
#include "Arduino.h"
#define NO_KEY '\0'
#define makeKeymap(x) ((char*)x)
class Keypad {
 public:
  Keypad() {}
  Keypad(char* userKeymap, byte* row, byte* col, byte numRows, byte numCols) {
    (void)userKeymap; (void)row; (void)col; (void)numRows; (void)numCols;
  }
  char getKey();
  void setDebounceTime(unsigned int ms) { (void)ms; }
};

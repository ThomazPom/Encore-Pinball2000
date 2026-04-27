/*
 * Copyright (c) [2016], [Curt Meyers]
 * All rights reserved.
*/
#pragma once
#include <sys/io.h>
#include <unistd.h>
#include <stdint.h>

class HW {
 public:
  HW(unsigned base) : sim(false), BASE(base), STATUS(base+1), CONTROL(base+2) { };

  int init();

  inline void portIn() {
    ctl |= (DIRECTION | SELECT);
    if (!sim) {
      outb(ctl, CONTROL);
    }
  }

  inline void portOut() {
    ctl &= ~(DIRECTION | SELECT);
    if (!sim) {
      outb(ctl, CONTROL);
    }
  }

  void control(uint8_t reg);  // write to a control register
  void writeData(uint8_t data, long nsDelay = 0); // latch data
  void writeDataOnly(uint8_t data) { outb(data, BASE); }
  void readData(uint8_t *result);

  void setInit(bool high) {
    if (high) {
      outb((inb(CONTROL) | INIT), CONTROL);
    }
    else {
      outb((inb(CONTROL) & ~INIT), CONTROL);
    }
  }

  void setStrobe(bool high) {
    if (high) {
      outb((inb(CONTROL) & ~STROBE), CONTROL);
    }
    else {
      outb((inb(CONTROL) | STROBE), CONTROL); // stobe low   - hardware inverted
    }
  }

  void setSelect(bool high) {
    if (high) {
      outb((inb(CONTROL) & ~SELECT), CONTROL);
    }
    else {
      outb((inb(CONTROL) | SELECT), CONTROL); // select low   - hardware inverted
    }
  }


 private:
  bool sim;
  uint8_t ctl;

  const unsigned BASE;
  const unsigned STATUS;
  const unsigned CONTROL;

  // CONTROL register
  static const unsigned STROBE = 0x1;  //Inverted
  static const unsigned INIT = 0x4;
  static const unsigned SELECT = 0x8;  //Inverted
  static const unsigned DIRECTION = 0x20;


};

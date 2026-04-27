/*
 * Copyright (c) [2016], [Curt Meyers]
 * All rights reserved.
*/

#include <iostream>

#include "hw.h"

int HW::init() {

  if (!sim) {
    int result = ioperm(BASE,3,1);
    if (result) {
      std::cout << "Failed to open parallel port\n";
      return(-1);
    }
    result = ioperm(BASE+0x402,1,1);
    if (result) {
      std::cout << "Failed to change the mode\n";
      return(-1);
    }
    std::cout << "Opened Parallel port: " << std::hex << BASE << std::dec << std::endl;

    uint8_t old = inb(BASE+0x402);
    old &= 0x1f;
    outb(old | 0x20,BASE+0x402); // set mode

    //unsigned char data;
    //data = inb(BASE);
    outb(0,BASE); //initialize data

    // change direction to input
    //uint8_t oldctl = inb(CONTROL);
    ctl = (DIRECTION | SELECT); //Input mode
    //ctl &= ~INIT;
    //std::cout << "oldctl=" << std::hex << (unsigned)oldctl << std::dec << std::endl;
    outb(ctl, CONTROL);
  }

  return 0;
}

void HW::control(uint8_t reg) {

  portOut();
  
  if (!sim) {
    outb(reg,BASE);
    outb((ctl & ~INIT), CONTROL);
    outb((ctl | INIT), CONTROL);
  }
}

void HW::writeData(uint8_t data, long nsDelay) {

  if (!sim) {
    outb(data,BASE);
    outb((ctl | STROBE), CONTROL); // stobe low   - hardware inverted
    if (nsDelay) {
      struct timespec ts, now;
      clock_gettime(CLOCK_MONOTONIC_RAW, &now);
      ts.tv_nsec = now.tv_nsec + nsDelay;
      if (ts.tv_nsec >= 1e9) {
        ts.tv_nsec -= 1e9;
      }
      while (now.tv_nsec < ts.tv_nsec) {
        clock_gettime(CLOCK_MONOTONIC_RAW, &now);
      }
    }
    outb((ctl & ~STROBE), CONTROL);
  }
}

void HW::readData(uint8_t *result) {
  portIn();

  outb((ctl | STROBE), CONTROL); // stobe low   - hardware inverted
  *result = (uint8_t)inb(BASE);
  outb((ctl & ~STROBE), CONTROL);
}

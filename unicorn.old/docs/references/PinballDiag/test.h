/*
 * Copyright (c) [2016], [Curt Meyers]
 * All rights reserved.
*/
#pragma once
//#include <QtCore>
#include "hw.h"

class Test
{
public:
  HW hw;

  Test(unsigned base);

  void init() { hw.init(); };

  int testLamps(int loops);
  int testPort();
  //void run();
  void setNextTest(int test) { nextTest = test; };
  //void terminate() { keepRunning = 0; };

signals:
  void myMessage(const QString);

private:
  void anykey();
  int nextTest;
  int keepRunning;

};

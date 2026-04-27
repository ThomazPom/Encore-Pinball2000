/*
 * Copyright (c) [2016], [Curt Meyers]
 * All rights reserved.
*/
#include <iostream>
#include <sys/io.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <QtCore>
#include <QtWidgets>
#include <thread>
#include "mainwindow.h"
#include "test.h"

/*
 reference: http://as6edriver.sourceforge.net/Parallel-Port-Programming-HOWTO
*/

using namespace std;

Test::Test(unsigned base) : hw(base), keepRunning(1)
{
}

void Test::anykey()
{
  //QMessageBox message;
  //QPushButton *yesButton = message.addButton(QMessageBox::Yes);

  //message.exec();
  //emit myMessage("Continue?");
  qDebug() << "Press a key";
}

int Test::testPort()
{
  keepRunning = 1;

  hw.portIn();
  for (int loop(0); loop < 100 && keepRunning; loop++) {
#if 0
    qDebug() << "Basic Parallel port testing\n";
    qDebug() << "Ready to start? ";
    anykey();

    qDebug() << "Check the following signals for activity: \n";
    qDebug() << " STROBE\n";
#endif
    qDebug() << "loop=" << loop;
    std::this_thread::sleep_for(100ms);
  }
  return 0;
}

int Test::testLamps(int loops)
{
  uint8_t reg[] = { 0x8, // LAMP_COL
                    0x6, // LAMP ROWA
                    0x7}; //LAMP ROWB

  uint8_t column = 0;
  uint8_t row = 0;
  uint8_t lamps = 0xff;
  struct timespec turnOn, turnOff;

  clock_gettime( CLOCK_MONOTONIC_RAW, &turnOn);
  clock_gettime( CLOCK_MONOTONIC_RAW, &turnOff);

  for ( ; --loops; )
  {
    for (column = 0; column < 8; column++) 
    {
      hw.control(0x8);
      hw.writeData(0);  // OFF first

      //for (row = 0; row < 8; row++) 
      {
        hw.control(0x6);
        hw.writeData(lamps);
        hw.control(0x7);
        hw.writeData(lamps);

#if 0
        clock_gettime( CLOCK_MONOTONIC_RAW, &now);
        time_t delay = 800 - ((now.tv_sec * 1e9 + now.tv_nsec) - (turnOff.tv_sec * 1e9 + turnOff.tv_nsec))/10;
        if (delay > 800)
          std::cout << "oops: " << now.tv_sec << "  " << turnOff.tv_sec << "  " << now.tv_nsec << "  " << turnOff.tv_nsec << "\n";
        if (delay > 10)
        {
          std::cout << "sleep=" << delay << std::endl;
          usleep(delay);
        }
#endif
        hw.writeData(1 << column);  // ON 


        usleep(100);  // leave on for 100us
        //clock_gettime( CLOCK_MONOTONIC_RAW, &turnOff);
      }

      hw.control(0x5);  //SW_COL
      hw.writeData(1 << column);

      hw.control(0xD);  //SOL_D
      hw.writeData(0xff);
    }
    //usleep(10000);
  }

  return 0;
}

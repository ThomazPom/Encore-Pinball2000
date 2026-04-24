/*
 * Copyright (c) [2016], [Curt Meyers]
 * All rights reserved.
*/
#include <QDebug>
#include "test_thread.h"

void TestThread::doWork()
{
  //qDebug() << ("Start work");

  while (keepRunning) {
    // Try to reset watchdog
    hw->control(0x5); // trigger watchdog
    hw->writeData(0, 40000);

    hw->control(0xD);
    hw->writeData(0x30); // Health LED & relay

    struct timespec ts, now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    ts.tv_sec = now.tv_sec;
    ts.tv_nsec = now.tv_nsec + watchdogDelay_ * 1e6; // ms to nsec
    if (ts.tv_nsec >= 1e9) {
      ts.tv_nsec -= 1e9;
      ts.tv_sec += 1;
    }
    while ((now.tv_sec < ts.tv_sec) || (now.tv_nsec < ts.tv_nsec)) {
      clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    }
  }
  //qDebug() << "Finished";
  emit finished();
}


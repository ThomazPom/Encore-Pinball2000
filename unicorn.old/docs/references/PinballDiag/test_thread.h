/*
 * Copyright (c) [2016], [Curt Meyers]
 * All rights reserved.
*/
#ifndef THREADS_H
#define THREADS_H
#include <QtCore>

#include "hw.h"

class TestThread : public QObject
{
  Q_OBJECT

public
  :TestThread(HW *hw) : watchdogDelay_(50000) {
    this->hw = hw;
    keepRunning = true;
  }

  bool keepRunning;
  void setWatchdogDelay(long watchdogDelay) { watchdogDelay_ = watchdogDelay; }

public slots:
  void doWork();

signals:
  void finished();

private:
  HW *hw;
  long watchdogDelay_;

};

#endif // THREADS_H


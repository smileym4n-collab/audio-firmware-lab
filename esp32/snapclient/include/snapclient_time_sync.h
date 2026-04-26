#pragma once

#include <math.h>

#include <Arduino.h>

#include "api/SnapTimeSync.h"

class SnapTimeSyncClampedDynamicSinceStart
    : public snap_arduino::SnapTimeSyncDynamicSinceStart {
 public:
  SnapTimeSyncClampedDynamicSinceStart(int processingLag,
                                       int interval,
                                       float minFactor,
                                       float maxFactor,
                                       float unityDeadband)
      : snap_arduino::SnapTimeSyncDynamicSinceStart(processingLag, interval),
        minFactor_(minFactor),
        maxFactor_(maxFactor),
        unityDeadband_(unityDeadband) {}

  float getFactor() override {
    float factor =
        snap_arduino::SnapTimeSyncDynamicSinceStart::getFactor();
    if (fabsf(factor - 1.0f) <= unityDeadband_) {
      factor = 1.0f;
    }

    if (factor < minFactor_) {
      factor = minFactor_;
    } else if (factor > maxFactor_) {
      factor = maxFactor_;
    }
    return factor;
  }

 private:
  float minFactor_ = 0.9990f;
  float maxFactor_ = 1.0010f;
  float unityDeadband_ = 0.0002f;
};

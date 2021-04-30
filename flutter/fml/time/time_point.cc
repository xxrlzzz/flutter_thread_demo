// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/fml/time/time_point.h"

#include "flutter/fml/build_config.h"

#include <chrono>

namespace fml {

TimePoint TimePoint::Now() {
  // The base time is arbitrary; use the clock epoch for convenience.
  const auto elapsed_time = std::chrono::steady_clock::now().time_since_epoch();
  return TimePoint(
      std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed_time)
          .count());
}

} // namespace fml

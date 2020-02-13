// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/fml/platform/fuchsia/message_loop_fuchsia.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/zx/time.h>

namespace fml {

MessageLoopFuchsia::MessageLoopFuchsia()
  : running_(false) {
  auto status = zx_timer_create(0, ZX_CLOCK_MONOTONIC, &timer_);
  FML_DCHECK(status == ZX_OK);
}

MessageLoopFuchsia::~MessageLoopFuchsia() = default;

void MessageLoopFuchsia::Run() {
  zx_signals_t observed;
  running_ = true;
  while (running_) {
    auto status = zx_object_wait_one(timer_, ZX_TIMER_SIGNALED, ZX_TIME_INFINITE, &observed);

    if (status == ZX_OK) {
      RunExpiredTasksNow();
    }
  }
}

void MessageLoopFuchsia::Terminate() {
  running_ = false;
}

void MessageLoopFuchsia::WakeUp(fml::TimePoint time_point) {

  fml::TimePoint now = fml::TimePoint::Now();
  zx_duration_t delay = 0;
  if (time_point > now) {
    delay = (time_point - now).ToNanoseconds();
  }
  auto due_time = zx_deadline_after(delay);
  auto status = zx_timer_set(timer_, due_time, ZX_TIMER_SLACK_CENTER);
  FML_DCHECK(status == ZX_OK);
}

}  // namespace fml

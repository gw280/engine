// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/fml/platform/fuchsia/message_loop_fuchsia.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/zx/time.h>

namespace fml {

MessageLoopFuchsia::MessageLoopFuchsia()
    : loop_(&kAsyncLoopConfigAttachToCurrentThread), running_(false) {}

MessageLoopFuchsia::~MessageLoopFuchsia() = default;

void MessageLoopFuchsia::Run() {
  running_ = true;
  while (running_) {
    auto status = loop_.Run();
    if (status == ZX_ERR_TIMED_OUT || status == ZX_ERR_CANCELED || status == ZX_ERR_BAD_STATE) {
      RunExpiredTasksNow();
      running_ = false;
    }
  }
}

void MessageLoopFuchsia::Terminate() {
  running_ = false;
  loop_.Quit();
}

void MessageLoopFuchsia::WakeUp(fml::TimePoint time_point) {
  fml::TimePoint now = fml::TimePoint::Now();
  zx::duration due_time{0};
  if (time_point > now) {
    due_time = zx::nsec((time_point - now).ToNanoseconds());
  }

  auto status = async::PostDelayedTask(
      loop_.dispatcher(), [this]() { RunExpiredTasksNow(); }, due_time);
  FML_DCHECK(status == ZX_OK);
}

}  // namespace fml

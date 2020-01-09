// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/common/shell_test_platform_view_vulkan.h"

namespace flutter {
namespace testing {

ShellTestPlatformViewVulkan::ShellTestPlatformViewVulkan(
    PlatformView::Delegate& delegate,
    TaskRunners task_runners,
    std::shared_ptr<ShellTestVsyncClock> vsync_clock,
    CreateVsyncWaiter create_vsync_waiter)
    : ShellTestPlatformView(delegate, std::move(task_runners)),
      create_vsync_waiter_(std::move(create_vsync_waiter)),
      vsync_clock_(vsync_clock) {}

ShellTestPlatformViewVulkan::~ShellTestPlatformViewVulkan() = default;

std::unique_ptr<VsyncWaiter> ShellTestPlatformViewVulkan::CreateVSyncWaiter() {
  return create_vsync_waiter_();
}

void ShellTestPlatformViewVulkan::SimulateVSync() {
  vsync_clock_->SimulateVSync();
}

// |PlatformView|
std::unique_ptr<Surface> ShellTestPlatformViewVulkan::CreateRenderingSurface() {
  return nullptr;
}

// |PlatformView|
PointerDataDispatcherMaker ShellTestPlatformViewVulkan::GetDispatcherMaker() {
  return [](DefaultPointerDataDispatcher::Delegate& delegate) {
    return std::make_unique<SmoothPointerDataDispatcher>(delegate);
  };
}

}  // namespace testing
}  // namespace flutter

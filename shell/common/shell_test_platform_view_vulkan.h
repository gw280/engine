// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_COMMON_SHELL_TEST_PLATFORM_VIEW_VULKAN_H_
#define FLUTTER_SHELL_COMMON_SHELL_TEST_PLATFORM_VIEW_VULKAN_H_

#include "flutter/shell/common/shell_test_platform_view.h"

namespace flutter {
namespace testing {

class ShellTestPlatformViewVulkan : public ShellTestPlatformView {
 public:
  ShellTestPlatformViewVulkan(PlatformView::Delegate& delegate,
                        TaskRunners task_runners,
                        std::shared_ptr<ShellTestVsyncClock> vsync_clock,
                        CreateVsyncWaiter create_vsync_waiter);

  virtual ~ShellTestPlatformViewVulkan() override;

  virtual void SimulateVSync() override;

 private:
  CreateVsyncWaiter create_vsync_waiter_;

  std::shared_ptr<ShellTestVsyncClock> vsync_clock_;

  // |PlatformView|
  std::unique_ptr<Surface> CreateRenderingSurface() override;

  // |PlatformView|
  std::unique_ptr<VsyncWaiter> CreateVSyncWaiter() override;

  // |PlatformView|
  PointerDataDispatcherMaker GetDispatcherMaker() override;

  FML_DISALLOW_COPY_AND_ASSIGN(ShellTestPlatformViewVulkan);
};

}  // namespace testing
}  // namespace flutter

#endif  // FLUTTER_SHELL_COMMON_SHELL_TEST_PLATFORM_VIEW_VULKAN_H_

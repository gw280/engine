// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/common/shell_test_platform_view.h"

#if OS_FUCHSIA
#include "flutter/shell/common/shell_test_platform_view_vulkan.h"
#else
#include "flutter/shell/common/shell_test_platform_view_gl.h"
#endif

namespace flutter {
namespace testing {

std::unique_ptr<ShellTestPlatformView> ShellTestPlatformView::Create(
    PlatformView::Delegate& delegate,
    TaskRunners task_runners,
    std::shared_ptr<ShellTestVsyncClock> vsync_clock,
    CreateVsyncWaiter create_vsync_waiter)
    {
#if OS_FUCHSIA
        return std::make_unique<ShellTestPlatformViewVulkan>(delegate,
        task_runners,
        vsync_clock,
        create_vsync_waiter);

#else
        return std::make_unique<ShellTestPlatformViewGL>(delegate,
        task_runners,
        vsync_clock,
        create_vsync_waiter);
#endif
    }


}  // namespace testing
}  // namespace flutter

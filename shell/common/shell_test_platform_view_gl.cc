// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/common/shell_test_platform_view_gl.h"

ShellTestPlatformViewGL::ShellTestPlatformViewGL(
    PlatformView::Delegate& delegate,
    TaskRunners task_runners,
    std::shared_ptr<ShellTestVsyncClock> vsync_clock,
    CreateVsyncWaiter create_vsync_waiter)
    : PlatformView(delegate, std::move(task_runners)),
      gl_surface_(SkISize::Make(800, 600)),
      create_vsync_waiter_(std::move(create_vsync_waiter)),
      vsync_clock_(vsync_clock) {}

ShellTestPlatformViewGL::~ShellTestPlatformViewGL() = default;

std::unique_ptr<VsyncWaiter> ShellTestPlatformViewGL::CreateVSyncWaiter() {
  return create_vsync_waiter_();
}

void ShellTestPlatformViewGL::SimulateVSync() {
  vsync_clock_->SimulateVSync();
}

// |PlatformView|
std::unique_ptr<Surface> ShellTestPlatformViewGL::CreateRenderingSurface() {
  return std::make_unique<GPUSurfaceGL>(this, true);
}

// |PlatformView|
PointerDataDispatcherMaker ShellTestPlatformViewGL::GetDispatcherMaker() {
  return [](DefaultPointerDataDispatcher::Delegate& delegate) {
    return std::make_unique<SmoothPointerDataDispatcher>(delegate);
  };
}

// |GPUSurfaceGLDelegate|
bool ShellTestPlatformViewGL::GLContextMakeCurrent() {
  return gl_surface_.MakeCurrent();
}

// |GPUSurfaceGLDelegate|
bool ShellTestPlatformViewGL::GLContextClearCurrent() {
  return gl_surface_.ClearCurrent();
}

// |GPUSurfaceGLDelegate|
bool ShellTestPlatformViewGL::GLContextPresent() {
  return gl_surface_.Present();
}

// |GPUSurfaceGLDelegate|
intptr_t ShellTestPlatformViewGL::GLContextFBO() const {
  return gl_surface_.GetFramebuffer();
}

// |GPUSurfaceGLDelegate|
GPUSurfaceGLDelegate::GLProcResolver ShellTestPlatformViewGL::GetGLProcResolver()
    const {
  return [surface = &gl_surface_](const char* name) -> void* {
    return surface->GetProcAddress(name);
  };
}

// |GPUSurfaceGLDelegate|
ExternalViewEmbedder* ShellTestPlatformViewGL::GetExternalViewEmbedder() {
  return nullptr;
}
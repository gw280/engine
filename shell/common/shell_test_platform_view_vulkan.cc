// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/common/shell_test_platform_view_vulkan.h"
#include "flutter/shell/common/persistent_cache.h"

namespace flutter {
namespace testing {

// Default maximum number of budgeted resources in the cache.
static const int kGrCacheMaxCount = 8192;

// Default maximum number of bytes of GPU memory of budgeted resources in the
// cache.
// The shell will dynamically increase or decrease this cache based on the
// viewport size, unless a user has specifically requested a size on the Skia
// system channel.
static const size_t kGrCacheMaxByteSize = 24 * (1 << 20);

ShellTestPlatformViewVulkan::ShellTestPlatformViewVulkan(
    PlatformView::Delegate& delegate,
    TaskRunners task_runners,
    std::shared_ptr<ShellTestVsyncClock> vsync_clock,
    CreateVsyncWaiter create_vsync_waiter)
    : ShellTestPlatformView(delegate, std::move(task_runners)),
      create_vsync_waiter_(std::move(create_vsync_waiter)),
      vsync_clock_(vsync_clock),
      proc_table_(fml::MakeRefCounted<vulkan::VulkanProcTable>()) {}

ShellTestPlatformViewVulkan::~ShellTestPlatformViewVulkan() = default;

std::unique_ptr<VsyncWaiter> ShellTestPlatformViewVulkan::CreateVSyncWaiter() {
  return create_vsync_waiter_();
}

void ShellTestPlatformViewVulkan::SimulateVSync() {
  vsync_clock_->SimulateVSync();
}

// |PlatformView|
std::unique_ptr<Surface> ShellTestPlatformViewVulkan::CreateRenderingSurface() {
  return std::make_unique<OffScreenSurface>(proc_table_);
}

// |PlatformView|
PointerDataDispatcherMaker ShellTestPlatformViewVulkan::GetDispatcherMaker() {
  return [](DefaultPointerDataDispatcher::Delegate& delegate) {
    return std::make_unique<SmoothPointerDataDispatcher>(delegate);
  };
}

ShellTestPlatformViewVulkan::OffScreenSurface::OffScreenSurface(
    fml::RefPtr<vulkan::VulkanProcTable> vk)
    : valid_(false), vk_(std::move(vk)) {
  if (!vk_ || !vk_->HasAcquiredMandatoryProcAddresses()) {
    FML_DLOG(ERROR) << "Proc table has not acquired mandatory proc addresses.";
    return;
  }

  // Create the application instance.
  std::vector<std::string> extensions = {};

  application_ = std::make_unique<vulkan::VulkanApplication>(
      *vk_, "FlutterTest", std::move(extensions));

  if (!application_->IsValid() || !vk_->AreInstanceProcsSetup()) {
    // Make certain the application instance was created and it setup the
    // instance proc table entries.
    FML_DLOG(ERROR) << "Instance proc addresses have not been setup.";
    return;
  }

  // Create the device.

  logical_device_ = application_->AcquireFirstCompatibleLogicalDevice();

  if (logical_device_ == nullptr || !logical_device_->IsValid() ||
      !vk_->AreDeviceProcsSetup()) {
    // Make certain the device was created and it setup the device proc table
    // entries.
    FML_DLOG(ERROR) << "Device proc addresses have not been setup.";
    return;
  }

  // Create the Skia GrContext.
  if (!CreateSkiaGrContext()) {
    FML_DLOG(ERROR) << "Could not create Skia context.";
    return;
  }

  valid_ = true;
}

bool ShellTestPlatformViewVulkan::OffScreenSurface::CreateSkiaGrContext() {
  GrVkBackendContext backend_context;

  if (!CreateSkiaBackendContext(&backend_context)) {
    FML_DLOG(ERROR) << "Could not create Skia backend context.";
    return false;
  }
  GrContextOptions options;
  if (PersistentCache::cache_sksl()) {
    FML_LOG(INFO) << "Cache SkSL";
    options.fShaderCacheStrategy = GrContextOptions::ShaderCacheStrategy::kSkSL;
  }
    PersistentCache::MarkStrategySet();

    options.fPersistentCache = PersistentCache::GetCacheForProcess();

  sk_sp<GrContext> context = GrContext::MakeVulkan(backend_context, options);

  if (context == nullptr) {
    FML_DLOG(ERROR) << "Failed to create GrContext";
    return false;
  }

  context->setResourceCacheLimits(kGrCacheMaxCount, kGrCacheMaxByteSize);

  context_ = context;
  std::vector<PersistentCache::SkSLCache> caches =
      PersistentCache::GetCacheForProcess()->LoadSkSLs();
  int compiled_count = 0;
  for (const auto& cache : caches) {
    compiled_count += context_->precompileShader(*cache.first, *cache.second);
  }
  FML_LOG(INFO) << "Found " << caches.size() << " SkSL shaders; precompiled "
                << compiled_count;

  return true;
}

bool ShellTestPlatformViewVulkan::OffScreenSurface::CreateSkiaBackendContext(
    GrVkBackendContext* context) {
  auto getProc = vk_->CreateSkiaGetProc();

  if (getProc == nullptr) {
    FML_DLOG(ERROR) << "GetProcAddress is null";
    return false;
  }

  uint32_t skia_features = 0;
  if (!logical_device_->GetPhysicalDeviceFeaturesSkia(&skia_features)) {
    FML_DLOG(ERROR) << "Failed to get Physical Device features";
    return false;
  }

  context->fInstance = application_->GetInstance();
  context->fPhysicalDevice = logical_device_->GetPhysicalDeviceHandle();
  context->fDevice = logical_device_->GetHandle();
  context->fQueue = logical_device_->GetQueueHandle();
  context->fGraphicsQueueIndex = logical_device_->GetGraphicsQueueIndex();
  context->fMinAPIVersion = application_->GetAPIVersion();
  context->fMaxAPIVersion = application_->GetAPIVersion();
  context->fFeatures = skia_features;
  context->fGetProc = std::move(getProc);
  context->fOwnsInstanceAndDevice = false;
  return true;
}

ShellTestPlatformViewVulkan::OffScreenSurface::~OffScreenSurface() {}

bool ShellTestPlatformViewVulkan::OffScreenSurface::IsValid() {
  return valid_;
}

std::unique_ptr<SurfaceFrame>
ShellTestPlatformViewVulkan::OffScreenSurface::AcquireFrame(
    const SkISize& size) {
  FML_LOG(INFO) << "size" << size.width() << " " << size.height();
  auto image_info = SkImageInfo::Make(size, SkColorType::kRGBA_8888_SkColorType,
                                      SkAlphaType::kOpaque_SkAlphaType);
  auto surface = SkSurface::MakeRenderTarget(context_.get(), SkBudgeted::kNo,
                                             image_info, 0, nullptr);
  SurfaceFrame::SubmitCallback callback =
      [](const SurfaceFrame&, SkCanvas* canvas) -> bool { return true; };

  return std::make_unique<SurfaceFrame>(std::move(surface), true,
                                        std::move(callback));
}

GrContext* ShellTestPlatformViewVulkan::OffScreenSurface::GetContext() {
  return context_.get();
}

SkMatrix ShellTestPlatformViewVulkan::OffScreenSurface::GetRootTransformation()
    const {
  SkMatrix matrix;
  matrix.reset();
  return matrix;
}

}  // namespace testing
}  // namespace flutter

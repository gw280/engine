// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_FLOW_SCENE_UPDATE_CONTEXT_H_
#define FLUTTER_FLOW_SCENE_UPDATE_CONTEXT_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "flutter/flow/embedded_views.h"
#include "flutter/fml/logging.h"
#include "flutter/fml/macros.h"
#include "third_party/skia/include/core/SkSurface.h"

#if defined(LEGACY_FUCHSIA_EMBEDDER)
#include "third_party/skia/include/core/SkColor.h"   // nogncheck
#include "third_party/skia/include/core/SkRect.h"    // nogncheck
#include "third_party/skia/include/core/SkScalar.h"  // nogncheck
#else
#include "flutter/flow/canvas_spy.h"                          // nogncheck
#include "third_party/skia/include/core/SkPictureRecorder.h"  // nogncheck
#endif

namespace flutter {

#if defined(LEGACY_FUCHSIA_EMBEDDER)
class Layer;
#endif

// Scenic currently lacks an API to enable rendering of alpha channel; this
// only happens if there is a OpacityNode higher in the tree with opacity
// != 1. For now, clamp to a infinitesimally smaller value than 1, which does
// not cause visual problems in practice.
constexpr float kOneMinusEpsilon = 1 - FLT_EPSILON;

// How much layers are separated in Scenic z elevation.
constexpr float kScenicZElevationBetweenLayers = 0.1f;

class SessionWrapper {
 public:
  virtual ~SessionWrapper() {}

  virtual scenic::Session* get() = 0;
  virtual void Present() = 0;
};

class SceneUpdateContext : public flutter::ExternalViewEmbedder {
 public:
#if defined(LEGACY_FUCHSIA_EMBEDDER)
  class Entity {
   public:
    Entity(SceneUpdateContext& context);
    virtual ~Entity();

    SceneUpdateContext& context() { return context_; }
    scenic::EntityNode& entity_node() { return entity_node_; }
    virtual scenic::ContainerNode& embedder_node() { return entity_node_; }

   private:
    SceneUpdateContext& context_;
    Entity* const previous_entity_;

    scenic::EntityNode entity_node_;
  };

  class Transform : public Entity {
   public:
    Transform(SceneUpdateContext& context, const SkMatrix& transform);
    Transform(SceneUpdateContext& context,
              float scale_x,
              float scale_y,
              float scale_z);
    virtual ~Transform();

   private:
    float const previous_scale_x_;
    float const previous_scale_y_;
  };

  class Frame : public Entity {
   public:
    // When layer is not nullptr, the frame is associated with a layer subtree
    // rooted with that layer. The frame may then create a surface that will
    // be retained for that layer.
    Frame(SceneUpdateContext& context,
          const SkRRect& rrect,
          SkColor color,
          SkAlpha opacity,
          std::string label);
    virtual ~Frame();

    scenic::ContainerNode& embedder_node() override { return opacity_node_; }

    void AddPaintLayer(Layer* layer);

   private:
    const float previous_elevation_;

    const SkRRect rrect_;
    SkColor const color_;
    SkAlpha const opacity_;

    scenic::OpacityNodeHACK opacity_node_;
    std::vector<Layer*> paint_layers_;
    SkRect paint_bounds_;
  };

  class Clip : public Entity {
   public:
    Clip(SceneUpdateContext& context, const SkRect& shape_bounds);
    ~Clip() = default;
  };

  struct PaintTask {
    SkRect paint_bounds;
    SkScalar scale_x;
    SkScalar scale_y;
    SkColor background_color;
    scenic::Material material;
    std::vector<Layer*> layers;
  };
#else
  struct CompositorSurface {
    scenic::Image* compositor_image;
    sk_sp<SkSurface> sk_surface;
  };
  using CreateSurfaceCallback =
      std::function<CompositorSurface(const SkISize&)>;
  using PresentSurfacesCallback = std::function<void()>;
#endif

  SceneUpdateContext(std::string debug_label,
                     fuchsia::ui::views::ViewToken view_token,
                     scenic::ViewRefPair view_ref_pair,
#if !defined(LEGACY_FUCHSIA_EMBEDDER)
                     CreateSurfaceCallback create_surface,
                     PresentSurfacesCallback present_surfaces,
#endif
                     SessionWrapper& session);
  ~SceneUpdateContext() = default;

#if defined(LEGACY_FUCHSIA_EMBEDDER)
  // The cumulative alpha value based on all the parent OpacityLayers.
  void set_alphaf(float alpha) { alpha_ = alpha; }
  float alphaf() { return alpha_; }

  // Returns all `PaintTask`s generated for the current frame.
  std::vector<PaintTask> GetPaintTasks();
#endif

  // Enable/disable wireframe rendering around the root view bounds.
  void EnableWireframe(bool enable);

  // Reset state for a new frame.
  void Reset();

  // |ExternalViewEmbedder|
  SkCanvas* GetRootCanvas() override;

  // |ExternalViewEmbedder|
  void CancelFrame() override;

  // |ExternalViewEmbedder|
  void BeginFrame(
      SkISize frame_size,
      GrDirectContext* context,
      double device_pixel_ratio,
      fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger) override;

  // |ExternalViewEmbedder|
  void SubmitFrame(GrDirectContext* context,
                   std::unique_ptr<SurfaceFrame> frame) override;

  // |ExternalViewEmbedder|
  void EndFrame(
      bool should_resubmit_frame,
      fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger) override;

  // |ExternalViewEmbedder|
  void PrerollCompositeEmbeddedView(
      int view_id,
      std::unique_ptr<EmbeddedViewParams> params) override;

  // |ExternalViewEmbedder|
  SkCanvas* CompositeEmbeddedView(int view_id) override;

  // |ExternalViewEmbedder|
  std::vector<SkCanvas*> GetCurrentCanvases() override;

  void CreateView(int64_t view_id, bool hit_testable, bool focusable);
  void DestroyView(int64_t view_id);
#if defined(LEGACY_FUCHSIA_EMBEDDER)
  void UpdateView(int64_t view_id,
                  const SkPoint& offset,
                  const SkSize& size,
                  std::optional<bool> override_hit_testable = std::nullopt);
#endif

 private:
  SessionWrapper& session_;

  scenic::View root_view_;
  scenic::EntityNode root_node_;

#if defined(LEGACY_FUCHSIA_EMBEDDER)
  std::vector<PaintTask> paint_tasks_;

  Entity* top_entity_ = nullptr;
  float top_scale_x_ = 1.f;
  float top_scale_y_ = 1.f;
  float top_elevation_ = 0.f;

  float next_elevation_ = 0.f;
  float alpha_ = 1.f;
#else
  struct CompositorLayer {
    CompositorLayer(const SkISize& frame_size,
                    std::optional<EmbeddedViewParams> view_params)
        : embedded_view_params(std::move(view_params)),
          recorder(std::make_unique<SkPictureRecorder>()),
          canvas_spy(std::make_unique<CanvasSpy>(
              recorder->beginRecording(frame_size.width(),
                                       frame_size.height()))),
          surface_size(frame_size) {}

    std::optional<EmbeddedViewParams> embedded_view_params;
    std::unique_ptr<SkPictureRecorder> recorder;
    std::unique_ptr<CanvasSpy> canvas_spy;
    SkISize surface_size;
  };
  struct CompositorShape {
    scenic::ShapeNode shape_node;
    scenic::Material material;
  };
  using CompositorLayerId = std::optional<int64_t>;

  std::unordered_map<CompositorLayerId, CompositorLayer> frame_layers_;
  std::vector<CompositorShape> scenic_shapes_;
  std::vector<CompositorLayerId> composition_order_;

  CreateSurfaceCallback create_surface_callback_;
  PresentSurfacesCallback present_surfaces_callback_;

  SkISize pending_frame_size_ = SkISize::Make(0, 0);
  float pending_device_pixel_ratio_ = 1.f;
#endif

  FML_DISALLOW_COPY_AND_ASSIGN(SceneUpdateContext);
};

}  // namespace flutter

#endif  // FLUTTER_FLOW_SCENE_UPDATE_CONTEXT_H_

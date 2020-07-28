// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scene_update_context.h"

#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>
#include "flutter/fml/trace_event.h"

#include "view_holder.h"

#if defined(LEGACY_FUCHSIA_EMBEDDER)
#include "flutter/flow/layers/layer.h"          // nogncheck
#include "flutter/flow/matrix_decomposition.h"  // nogncheck
#else
#include <lib/ui/scenic/cpp/resources.h>              // nogncheck
#include "third_party/skia/include/core/SkSurface.h"  // nogncheck
#include "third_party/skia/include/gpu/GrContext.h"   // nogncheck
#endif

namespace flutter {
namespace {

#if defined(LEGACY_FUCHSIA_EMBEDDER)
void SetEntityNodeClipPlanes(scenic::EntityNode& entity_node,
                             const SkRect& bounds) {
  const float top = bounds.top();
  const float bottom = bounds.bottom();
  const float left = bounds.left();
  const float right = bounds.right();

  // We will generate 4 oriented planes, one for each edge of the bounding rect.
  std::vector<fuchsia::ui::gfx::Plane3> clip_planes;
  clip_planes.resize(4);

  // Top plane.
  clip_planes[0].dist = top;
  clip_planes[0].dir.x = 0.f;
  clip_planes[0].dir.y = 1.f;
  clip_planes[0].dir.z = 0.f;

  // Bottom plane.
  clip_planes[1].dist = -bottom;
  clip_planes[1].dir.x = 0.f;
  clip_planes[1].dir.y = -1.f;
  clip_planes[1].dir.z = 0.f;

  // Left plane.
  clip_planes[2].dist = left;
  clip_planes[2].dir.x = 1.f;
  clip_planes[2].dir.y = 0.f;
  clip_planes[2].dir.z = 0.f;

  // Right plane.
  clip_planes[3].dist = -right;
  clip_planes[3].dir.x = -1.f;
  clip_planes[3].dir.y = 0.f;
  clip_planes[3].dir.z = 0.f;

  entity_node.SetClipPlanes(std::move(clip_planes));
}

void SetMaterialColor(scenic::Material& material,
                      SkColor color,
                      SkAlpha opacity) {
  const SkAlpha color_alpha = static_cast<SkAlpha>(
      ((float)SkColorGetA(color) * (float)opacity) / 255.0f);
  material.SetColor(SkColorGetR(color), SkColorGetG(color), SkColorGetB(color),
                    color_alpha);
}

std::optional<SceneUpdateContext::PaintTask> CreateFrame(
    scenic::Session* session,
    scenic::EntityNode& entity_node,
    const SkRRect& rrect,
    SkColor color,
    SkAlpha opacity,
    SkSize scale,
    const SkRect& paint_bounds,
    std::vector<Layer*> paint_layers) {
  if (rrect.isEmpty())
    return std::nullopt;

  std::optional<SceneUpdateContext::PaintTask> paint_task;

  // Frames always clip their children.
  SkRect shape_bounds = rrect.getBounds();
  SetEntityNodeClipPlanes(entity_node, shape_bounds);

  // and possibly for its texture.
  // TODO(SCN-137): Need to be able to express the radii as vectors.
  scenic::ShapeNode shape_node(session);
  scenic::Rectangle shape(session, rrect.width(), rrect.height());
  shape_node.SetShape(shape);
  shape_node.SetTranslation(shape_bounds.width() * 0.5f + shape_bounds.left(),
                            shape_bounds.height() * 0.5f + shape_bounds.top(),
                            0.f);

  // Check whether the painted layers will be visible.
  if (paint_bounds.isEmpty() || !paint_bounds.intersects(shape_bounds))
    paint_layers.clear();

  scenic::Material material(session);
  shape_node.SetMaterial(material);
  entity_node.AddChild(shape_node);

  // Check whether a solid color will suffice.
  if (paint_layers.empty() || shape_bounds.isEmpty()) {
    SetMaterialColor(material, color, opacity);
  } else {
    // The final shape's color is material_color * texture_color.  The passed in
    // material color was already used as a background when generating the
    // texture, so set the model color to |SK_ColorWHITE| in order to allow
    // using the texture's color unmodified.
    SetMaterialColor(material, SK_ColorWHITE, opacity);

    // Enqueue a paint task for these layers, to apply a texture to the whole
    // shape.
    paint_task.emplace(
        SceneUpdateContext::PaintTask{.paint_bounds = paint_bounds,
                                      .scale_x = scale.width(),
                                      .scale_y = scale.height(),
                                      .background_color = color,
                                      .material = std::move(material),
                                      .layers = std::move(paint_layers)});
  }

  return paint_task;
}
#endif

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                          sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

}  // namespace

SceneUpdateContext::SceneUpdateContext(std::string debug_label,
                                       fuchsia::ui::views::ViewToken view_token,
                                       scenic::ViewRefPair view_ref_pair,
#if !defined(LEGACY_FUCHSIA_EMBEDDER)
                                       CreateSurfaceCallback create_surface,
                                       PresentSurfacesCallback present_surfaces,
#endif
                                       SessionWrapper& session)
    : session_(session),
      root_view_(session_.get(),
                 std::move(view_token),
                 std::move(view_ref_pair.control_ref),
                 std::move(view_ref_pair.view_ref),
                 debug_label),
      root_node_(session_.get())
#if !defined(LEGACY_FUCHSIA_EMBEDDER)
      ,
      create_surface_callback_(std::move(create_surface)),
      present_surfaces_callback_(std::move(present_surfaces))
#endif
{
  root_view_.AddChild(root_node_);
  root_node_.SetEventMask(fuchsia::ui::gfx::kMetricsEventMask);
  root_node_.SetLabel("FlutterLayerTree");

  session_.Present();
}

#if defined(LEGACY_FUCHSIA_EMBEDDER)
std::vector<SceneUpdateContext::PaintTask> SceneUpdateContext::GetPaintTasks() {
  std::vector<PaintTask> frame_paint_tasks = std::move(paint_tasks_);

  paint_tasks_.clear();

  return frame_paint_tasks;
}
#endif

void SceneUpdateContext::EnableWireframe(bool enable) {
  session_.get()->Enqueue(
      scenic::NewSetEnableDebugViewBoundsCmd(root_view_.id(), enable));
}

void SceneUpdateContext::Reset() {
#if defined(LEGACY_FUCHSIA_EMBEDDER)
  paint_tasks_.clear();
  top_entity_ = nullptr;
  top_scale_x_ = 1.f;
  top_scale_y_ = 1.f;
  top_elevation_ = 0.f;
  next_elevation_ = 0.f;
  alpha_ = 1.f;
#else
  frame_layers_.clear();
  composition_order_.clear();
  pending_frame_size_ = SkISize::Make(0, 0);
  pending_device_pixel_ratio_ = 1.f;
#endif

  // We are going to be sending down a fresh node hierarchy every frame. So just
  // enqueue a detach op on the imported root node.
  session_.get()->Enqueue(scenic::NewDetachChildrenCmd(root_node_.id()));
}

SkCanvas* SceneUpdateContext::GetRootCanvas() {
#if defined(LEGACY_FUCHSIA_EMBEDDER)
  return nullptr;
#else
  constexpr auto kRootLayerId = CompositorLayerId{};
  auto found = frame_layers_.find(kRootLayerId);
  if (found == frame_layers_.end()) {
    FML_DLOG(WARNING)
        << "No root canvas could be found. This is extremely unlikely and "
           "indicates that the external view embedder did not receive the "
           "notification to begin the frame.";
    return nullptr;
  }
  return found->second.canvas_spy->GetSpyingCanvas();
#endif
}

void SceneUpdateContext::CancelFrame() {
#if !defined(LEGACY_FUCHSIA_EMBEDDER)
  Reset();
#endif
}

void SceneUpdateContext::BeginFrame(
    SkISize frame_size,
    GrDirectContext* context,
    double device_pixel_ratio,
    fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger) {
#if !defined(LEGACY_FUCHSIA_EMBEDDER)
  Reset();

  pending_frame_size_ = frame_size;
  pending_device_pixel_ratio_ = device_pixel_ratio;

  frame_layers_.emplace(std::make_pair(
      CompositorLayerId{}, CompositorLayer(frame_size, std::nullopt)));
  composition_order_.push_back(CompositorLayerId{});
#endif
}

void SceneUpdateContext::SubmitFrame(GrDirectContext* context,
                                     std::unique_ptr<SurfaceFrame> frame) {
#if defined(LEGACY_FUCHSIA_EMBEDDER)
  return;
#else
  // Re-scale everything according to the DPR.
  const float inv_dpr = 1.0f / pending_device_pixel_ratio_;
  root_node_.SetScale(inv_dpr, inv_dpr, 1.0f);

  // Create render targets for the frame.
  std::unordered_map<CompositorLayerId, CompositorSurface> frame_surfaces;
  for (const auto& layer : frame_layers_) {
    if (!layer.second.canvas_spy->DidDrawIntoCanvas()) {
      continue;
    }

    auto surface = create_surface_callback_(layer.second.surface_size);
    if (!surface.sk_surface || !surface.compositor_image) {
      FML_LOG(ERROR) << "Embedder did not return a valid render target.";
      continue;
    }

    frame_surfaces.emplace(std::make_pair(layer.first, std::move(surface)));
  }

  // In composition order, submit backing stores and platform views to Scenic.
  size_t shape_index = 0;
  float embedded_views_height = 0.0f;
  for (const auto& layer_id : composition_order_) {
    const auto& layer = frame_layers_.find(layer_id);
    FML_DCHECK(layer != frame_layers_.end());

    if (layer_id.has_value()) {
      zx_koid_t koid = GetKoid(static_cast<zx_handle_t>(layer_id.value()));
      FML_LOG(ERROR) << "Embedding View w/ handle " << layer_id.value()
                     << " and koid " << koid;
      auto* view_holder = ViewHolder::FromId(koid);
      FML_CHECK(view_holder);

      FML_CHECK(layer->second.embedded_view_params.has_value());
      auto& view_params = layer->second.embedded_view_params.value();
      SkPoint view_offset =
          SkPoint::Make(view_params.finalBoundingRect().left(),
                        view_params.finalBoundingRect().top());
      SkSize view_size = SkSize::Make(view_params.finalBoundingRect().width(),
                                      view_params.finalBoundingRect().height());
      FML_LOG(ERROR) << "View dimensions: " << view_offset.fX << "x"
                     << view_offset.fY << " and " << view_size.width() << "x"
                     << view_size.height();
      view_holder->SetProperties(view_size.width(), view_size.height(), 0, 0, 0,
                                 0, view_holder->focusable());
      view_holder->Update(
          session_.get(), root_node_, view_offset, view_size,
          (shape_index + 1) * -kScenicZElevationBetweenLayers,
          SK_AlphaOPAQUE,  // SkScalarRoundToInt(alphaf() * 255),
          view_holder->hit_testable());

      // Assume embedded views are just a single layer wide.
      embedded_views_height += -kScenicZElevationBetweenLayers;
    }

    if (layer->second.canvas_spy->DidDrawIntoCanvas()) {
      const auto& surface = frame_surfaces.find(layer_id);
      FML_DCHECK(surface != frame_surfaces.end());
      const auto* surface_image = surface->second.compositor_image;

      // Create a new shape if needed for the surface.
      FML_DCHECK(shape_index <= scenic_shapes_.size());
      if (shape_index == scenic_shapes_.size()) {
        CompositorShape new_shape{
            .shape_node = scenic::ShapeNode(session_.get()),
            .material = scenic::Material(session_.get()),
        };
        new_shape.shape_node.SetMaterial(new_shape.material);
        scenic_shapes_.emplace_back(std::move(new_shape));
      }

      // Set surface size and texture.
      auto& surface_shape = scenic_shapes_[shape_index];
      scenic::Rectangle rect(session_.get(), layer->second.surface_size.width(),
                             layer->second.surface_size.height());
      surface_shape.shape_node.SetLabel("FlutterLayer");
      surface_shape.shape_node.SetShape(rect);
      surface_shape.shape_node.SetTranslation(
          layer->second.surface_size.width() * 0.5f,
          layer->second.surface_size.height() * 0.5f,
          (shape_index + 1) * -kScenicZElevationBetweenLayers +
              embedded_views_height);
      surface_shape.material.SetColor(
          SkColorGetR(SK_ColorWHITE), SkColorGetG(SK_ColorWHITE),
          SkColorGetB(SK_ColorWHITE), SK_AlphaOPAQUE);
      surface_shape.material.SetTexture(*surface_image);

      root_node_.AddChild(surface_shape.shape_node);
    }
  }

  // Present the session to Scenic.
  session_.Present();

  // Scribble the recorded SkPictures into the render targets.
  for (const auto& surface : frame_surfaces) {
    const auto& layer = frame_layers_.find(surface.first);
    FML_DCHECK(layer != frame_layers_.end());

    auto picture = layer->second.recorder->finishRecordingAsPicture();
    FML_DCHECK(picture);

    auto sk_surface = surface.second.sk_surface;
    FML_DCHECK(sk_surface);

    FML_DCHECK(SkISize::Make(sk_surface->width(), sk_surface->height()) ==
               pending_frame_size_);

    auto canvas = sk_surface->getCanvas();
    FML_DCHECK(canvas);

    canvas->setMatrix(SkMatrix::I());
    canvas->clear(SK_ColorTRANSPARENT);
    canvas->drawPicture(picture);
    canvas->flush();
  }

  // Flush deferred Skia work and inform Scenic that the render targets are
  // ready.
  present_surfaces_callback_();

  frame->Submit();
#endif
}

void SceneUpdateContext::EndFrame(
    bool should_resubmit_frame,
    fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger) {}

void SceneUpdateContext::PrerollCompositeEmbeddedView(
    int view_id,
    std::unique_ptr<EmbeddedViewParams> params) {
#if !defined(LEGACY_FUCHSIA_EMBEDDER)
  FML_DCHECK(frame_layers_.count(view_id) == 0);

  frame_layers_.emplace(
      std::make_pair(CompositorLayerId{view_id},
                     CompositorLayer(pending_frame_size_, *params)));
  composition_order_.push_back(view_id);
#endif
}

SkCanvas* SceneUpdateContext::CompositeEmbeddedView(int view_id) {
#if defined(LEGACY_FUCHSIA_EMBEDDER)
  return nullptr;
#else
  auto found = frame_layers_.find(view_id);
  if (found == frame_layers_.end()) {
    FML_DCHECK(false) << "Attempted to composite a view that was not "
                         "pre-rolled.";
    return nullptr;
  }
  return found->second.canvas_spy->GetSpyingCanvas();
#endif
}

std::vector<SkCanvas*> SceneUpdateContext::GetCurrentCanvases() {
#if defined(LEGACY_FUCHSIA_EMBEDDER)
  return std::vector<SkCanvas*>();
#else
  std::vector<SkCanvas*> canvases;
  for (const auto& layer : frame_layers_) {
    // This method (for legacy reasons) expects non-root current canvases.
    if (layer.first.has_value()) {
      canvases.push_back(layer.second.canvas_spy->GetSpyingCanvas());
    }
  }
  return canvases;
#endif
}

void SceneUpdateContext::CreateView(int64_t view_id,
                                    bool hit_testable,
                                    bool focusable) {
  zx_handle_t handle = static_cast<zx_handle_t>(view_id);
  zx_koid_t koid = GetKoid(handle);
  FML_LOG(ERROR) << "Creating View w/ handle " << handle << " and koid "
                 << koid;
  flutter::ViewHolder::Create(
      koid, nullptr, scenic::ToViewHolderToken(zx::eventpair(handle)), nullptr);
  auto* view_holder = ViewHolder::FromId(koid);
  FML_CHECK(view_holder);

  view_holder->set_hit_testable(hit_testable);
  view_holder->set_focusable(focusable);
}

void SceneUpdateContext::DestroyView(int64_t view_id) {
  zx_koid_t koid = GetKoid(static_cast<zx_handle_t>(view_id));
  ViewHolder::Destroy(koid);
}

#if defined(LEGACY_FUCHSIA_EMBEDDER)
void SceneUpdateContext::UpdateView(int64_t view_id,
                                    const SkPoint& offset,
                                    const SkSize& size,
                                    std::optional<bool> override_hit_testable) {
  zx_koid_t koid = GetKoid(static_cast<zx_handle_t>(view_id));
  auto* view_holder = ViewHolder::FromId(koid);
  FML_DCHECK(view_holder);

  bool hit_testable = override_hit_testable.has_value()
                          ? override_hit_testable.value()
                          : view_holder->hit_testable();
  view_holder->SetProperties(size.width(), size.height(), 0, 0, 0, 0,
                             view_holder->focusable());
  view_holder->Update(session_.get(), top_entity_->embedder_node(), offset,
                      size, -kScenicZElevationBetweenLayers,
                      SkScalarRoundToInt(alphaf() * 255), hit_testable);
}
#endif

#if defined(LEGACY_FUCHSIA_EMBEDDER)
SceneUpdateContext::Entity::Entity(SceneUpdateContext& context)
    : context_(context),
      previous_entity_(context.top_entity_),
      entity_node_(context.session_.get()) {
  context.top_entity_ = this;
}

SceneUpdateContext::Entity::~Entity() {
  if (previous_entity_) {
    previous_entity_->embedder_node().AddChild(entity_node_);
  } else {
    context_.root_node_.AddChild(entity_node_);
  }

  FML_DCHECK(context_.top_entity_ == this);
  context_.top_entity_ = previous_entity_;
}

SceneUpdateContext::Transform::Transform(SceneUpdateContext& context,
                                         const SkMatrix& transform)
    : Entity(context),
      previous_scale_x_(context.top_scale_x_),
      previous_scale_y_(context.top_scale_y_) {
  entity_node().SetLabel("flutter::Transform");
  if (!transform.isIdentity()) {
    // TODO(SCN-192): The perspective and shear components in the matrix
    // are not handled correctly.
    MatrixDecomposition decomposition(transform);
    if (decomposition.IsValid()) {
      // Don't allow clients to control the z dimension; we control that
      // instead to make sure layers appear in proper order.
      entity_node().SetTranslation(decomposition.translation().x,  //
                                   decomposition.translation().y,  //
                                   0.f                             //
      );

      entity_node().SetScale(decomposition.scale().x,  //
                             decomposition.scale().y,  //
                             1.f                       //
      );
      context.top_scale_x_ *= decomposition.scale().x;
      context.top_scale_y_ *= decomposition.scale().y;

      entity_node().SetRotation(decomposition.rotation().x,  //
                                decomposition.rotation().y,  //
                                decomposition.rotation().z,  //
                                decomposition.rotation().w   //
      );
    }
  }
}

SceneUpdateContext::Transform::Transform(SceneUpdateContext& context,
                                         float scale_x,
                                         float scale_y,
                                         float scale_z)
    : Entity(context),
      previous_scale_x_(context.top_scale_x_),
      previous_scale_y_(context.top_scale_y_) {
  entity_node().SetLabel("flutter::Transform");
  if (scale_x != 1.f || scale_y != 1.f || scale_z != 1.f) {
    entity_node().SetScale(scale_x, scale_y, scale_z);
    context.top_scale_x_ *= scale_x;
    context.top_scale_y_ *= scale_y;
  }
}

SceneUpdateContext::Transform::~Transform() {
  context().top_scale_x_ = previous_scale_x_;
  context().top_scale_y_ = previous_scale_y_;
}

SceneUpdateContext::Frame::Frame(SceneUpdateContext& context,
                                 const SkRRect& rrect,
                                 SkColor color,
                                 SkAlpha opacity,
                                 std::string label)
    : Entity(context),
      previous_elevation_(context.top_elevation_),
      rrect_(rrect),
      color_(color),
      opacity_(opacity),
      opacity_node_(context.session_.get()),
      paint_bounds_(SkRect::MakeEmpty()) {
  context.top_elevation_ += kScenicZElevationBetweenLayers;
  context.next_elevation_ += kScenicZElevationBetweenLayers;

  entity_node().AddChild(opacity_node_);
  // Scenic currently lacks an API to enable rendering of alpha channel; alpha
  // channels are only rendered if there is a OpacityNode higher in the tree
  // with opacity != 1. For now, clamp to a infinitesimally smaller value than
  // 1, which does not cause visual problems in practice.
  opacity_node_.SetOpacity(std::min(kOneMinusEpsilon, opacity_ / 255.0f));
}

SceneUpdateContext::Frame::~Frame() {
  // Add a part which represents the frame's geometry for clipping purposes
  auto paint_task = CreateFrame(
      context().session_.get(), entity_node(), rrect_, color_, opacity_,
      SkSize::Make(context().top_scale_x_, context().top_scale_y_),
      paint_bounds_, std::move(paint_layers_));
  if (paint_task) {
    context().paint_tasks_.emplace_back(std::move(paint_task.value()));
  }

  context().top_elevation_ = previous_elevation_;
}

void SceneUpdateContext::Frame::AddPaintLayer(Layer* layer) {
  FML_DCHECK(layer->needs_painting());
  paint_layers_.push_back(layer);
  paint_bounds_.join(layer->paint_bounds());
}

SceneUpdateContext::Clip::Clip(SceneUpdateContext& context,
                               const SkRect& shape_bounds)
    : Entity(context) {
  entity_node().SetLabel("flutter::Clip");
  SetEntityNodeClipPlanes(entity_node(), shape_bounds);
}
#endif

}  // namespace flutter

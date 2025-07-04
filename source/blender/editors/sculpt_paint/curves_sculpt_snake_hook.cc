/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "curves_sculpt_intern.hh"

#include "BLI_math_geom.h"
#include "BLI_math_matrix_types.hh"
#include "BLI_vector.hh"

#include "DEG_depsgraph.hh"

#include "BKE_attribute.hh"
#include "BKE_brush.hh"
#include "BKE_context.hh"
#include "BKE_curves.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh_runtime.hh"
#include "BKE_paint.hh"

#include "DNA_brush_enums.h"
#include "DNA_brush_types.h"
#include "DNA_curves_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "ED_screen.hh"
#include "ED_view3d.hh"

#include "WM_api.hh"

/**
 * The code below uses a prefix naming convention to indicate the coordinate space:
 * - `cu`: Local space of the curves object that is being edited.
 * - `su`: Local space of the surface object.
 * - `wo`: World space.
 * - `re`: 2D coordinates within the region.
 */

namespace blender::ed::sculpt_paint {

using blender::bke::CurvesGeometry;

/**
 * Drags the tip point of each curve and resamples the rest of the curve.
 */
class SnakeHookOperation : public CurvesSculptStrokeOperation {
 private:
  float2 last_mouse_position_re_;

  CurvesBrush3D brush_3d_;

  friend struct SnakeHookOperatorExecutor;

 public:
  void on_stroke_extended(const bContext &C, const StrokeExtension &stroke_extension) override;
};

/**
 * Utility class that actually executes the update when the stroke is updated. That's useful
 * because it avoids passing a very large number of parameters between functions.
 */
struct SnakeHookOperatorExecutor {
  SnakeHookOperation *self_ = nullptr;
  CurvesSculptCommonContext ctx_;

  CurvesSculpt *curves_sculpt_ = nullptr;
  const Brush *brush_ = nullptr;
  float brush_radius_base_re_;
  float brush_radius_factor_;
  float brush_strength_;

  Object *object_ = nullptr;
  Curves *curves_id_ = nullptr;
  CurvesGeometry *curves_ = nullptr;

  VArray<float> curve_factors_;
  IndexMaskMemory selected_curve_memory_;
  IndexMask curve_selection_;

  CurvesSurfaceTransforms transforms_;

  float2 brush_pos_prev_re_;
  float2 brush_pos_re_;
  float2 brush_pos_diff_re_;

  SnakeHookOperatorExecutor(const bContext &C) : ctx_(C) {}

  void execute(SnakeHookOperation &self,
               const bContext &C,
               const StrokeExtension &stroke_extension)
  {
    BLI_SCOPED_DEFER([&]() { self.last_mouse_position_re_ = stroke_extension.mouse_position; });

    self_ = &self;
    object_ = CTX_data_active_object(&C);

    curves_sculpt_ = ctx_.scene->toolsettings->curves_sculpt;
    brush_ = BKE_paint_brush_for_read(&curves_sculpt_->paint);

    brush_radius_base_re_ = BKE_brush_size_get(&curves_sculpt_->paint, brush_);
    brush_radius_factor_ = brush_radius_factor(*brush_, stroke_extension);
    brush_strength_ = brush_strength_get(curves_sculpt_->paint, *brush_, stroke_extension);

    const eBrushFalloffShape falloff_shape = eBrushFalloffShape(brush_->falloff_shape);

    curves_id_ = static_cast<Curves *>(object_->data);
    curves_ = &curves_id_->geometry.wrap();
    if (curves_->is_empty()) {
      return;
    }

    transforms_ = CurvesSurfaceTransforms(*object_, curves_id_->surface);

    curve_factors_ = *curves_->attributes().lookup_or_default(
        ".selection", bke::AttrDomain::Curve, 1.0f);
    curve_selection_ = curves::retrieve_selected_curves(*curves_id_, selected_curve_memory_);

    brush_pos_prev_re_ = self.last_mouse_position_re_;
    brush_pos_re_ = stroke_extension.mouse_position;
    brush_pos_diff_re_ = brush_pos_re_ - brush_pos_prev_re_;

    if (stroke_extension.is_first) {
      if (falloff_shape == PAINT_FALLOFF_SHAPE_SPHERE || (U.uiflag & USER_ORBIT_SELECTION)) {
        std::optional<CurvesBrush3D> brush_3d = sample_curves_3d_brush(*ctx_.depsgraph,
                                                                       *ctx_.region,
                                                                       *ctx_.v3d,
                                                                       *ctx_.rv3d,
                                                                       *object_,
                                                                       brush_pos_re_,
                                                                       brush_radius_base_re_);
        if (brush_3d.has_value()) {
          self_->brush_3d_ = *brush_3d;
          remember_stroke_position(
              *curves_sculpt_,
              math::transform_point(transforms_.curves_to_world, self_->brush_3d_.position_cu));
        }
      }
      return;
    }

    if (falloff_shape == PAINT_FALLOFF_SHAPE_SPHERE) {
      this->spherical_snake_hook_with_symmetry();
    }
    else if (falloff_shape == PAINT_FALLOFF_SHAPE_TUBE) {
      this->projected_snake_hook_with_symmetry();
    }
    else {
      BLI_assert_unreachable();
    }

    curves_->tag_positions_changed();
    DEG_id_tag_update(&curves_id_->id, ID_RECALC_GEOMETRY);
    WM_main_add_notifier(NC_GEOM | ND_DATA, &curves_id_->id);
    ED_region_tag_redraw(ctx_.region);
  }

  void projected_snake_hook_with_symmetry()
  {
    const Vector<float4x4> symmetry_brush_transforms = get_symmetry_brush_transforms(
        eCurvesSymmetryType(curves_id_->symmetry));
    for (const float4x4 &brush_transform : symmetry_brush_transforms) {
      this->projected_snake_hook(brush_transform);
    }
  }

  void projected_snake_hook(const float4x4 &brush_transform)
  {
    const float4x4 brush_transform_inv = math::invert(brush_transform);
    const bke::crazyspace::GeometryDeformation deformation =
        bke::crazyspace::get_evaluated_curves_deformation(*ctx_.depsgraph, *object_);
    const OffsetIndices points_by_curve = curves_->points_by_curve();

    MutableSpan<float3> positions_cu = curves_->positions_for_write();

    const float4x4 projection = ED_view3d_ob_project_mat_get(ctx_.rv3d, object_);

    const float brush_radius_re = brush_radius_base_re_ * brush_radius_factor_;
    const float brush_radius_sq_re = pow2f(brush_radius_re);

    curve_selection_.foreach_segment(GrainSize(256), [&](const IndexMaskSegment segment) {
      MoveAndResampleBuffers resample_buffer;
      for (const int curve_i : segment) {
        const IndexRange points = points_by_curve[curve_i];
        const int last_point_i = points.last();
        const float3 old_pos_cu = deformation.positions[last_point_i];
        const float3 old_symm_pos_cu = math::transform_point(brush_transform_inv, old_pos_cu);

        const float2 old_symm_pos_re = ED_view3d_project_float_v2_m4(
            ctx_.region, old_symm_pos_cu, projection);

        const float distance_to_brush_sq_re = math::distance_squared(old_symm_pos_re,
                                                                     brush_pos_prev_re_);
        if (distance_to_brush_sq_re > brush_radius_sq_re) {
          continue;
        }

        const float radius_falloff = BKE_brush_curve_strength(
            brush_, std::sqrt(distance_to_brush_sq_re), brush_radius_re);
        const float weight = brush_strength_ * radius_falloff * curve_factors_[curve_i];

        const float2 new_symm_pos_re = old_symm_pos_re + brush_pos_diff_re_ * weight;
        float3 new_symm_pos_wo;
        ED_view3d_win_to_3d(ctx_.v3d,
                            ctx_.region,
                            math::transform_point(transforms_.curves_to_world, old_symm_pos_cu),
                            new_symm_pos_re,
                            new_symm_pos_wo);
        const float3 new_pos_cu = math::transform_point(
            brush_transform, math::transform_point(transforms_.world_to_curves, new_symm_pos_wo));
        const float3 translation_eval = new_pos_cu - old_pos_cu;
        const float3 translation_orig = deformation.translation_from_deformed_to_original(
            last_point_i, translation_eval);

        const float3 last_point_cu = positions_cu[last_point_i] + translation_orig;
        move_last_point_and_resample(resample_buffer, positions_cu.slice(points), last_point_cu);
      }
    });
  }

  void spherical_snake_hook_with_symmetry()
  {
    float3 brush_start_wo, brush_end_wo;
    ED_view3d_win_to_3d(
        ctx_.v3d,
        ctx_.region,
        math::transform_point(transforms_.curves_to_world, self_->brush_3d_.position_cu),
        brush_pos_prev_re_,
        brush_start_wo);
    ED_view3d_win_to_3d(
        ctx_.v3d,
        ctx_.region,
        math::transform_point(transforms_.curves_to_world, self_->brush_3d_.position_cu),
        brush_pos_re_,
        brush_end_wo);
    const float3 brush_start_cu = math::transform_point(transforms_.world_to_curves,
                                                        brush_start_wo);
    const float3 brush_end_cu = math::transform_point(transforms_.world_to_curves, brush_end_wo);

    const float brush_radius_cu = self_->brush_3d_.radius_cu * brush_radius_factor_;

    const Vector<float4x4> symmetry_brush_transforms = get_symmetry_brush_transforms(
        eCurvesSymmetryType(curves_id_->symmetry));
    for (const float4x4 &brush_transform : symmetry_brush_transforms) {
      this->spherical_snake_hook(math::transform_point(brush_transform, brush_start_cu),
                                 math::transform_point(brush_transform, brush_end_cu),
                                 brush_radius_cu);
    }
  }

  void spherical_snake_hook(const float3 &brush_start_cu,
                            const float3 &brush_end_cu,
                            const float brush_radius_cu)
  {
    const bke::crazyspace::GeometryDeformation deformation =
        bke::crazyspace::get_evaluated_curves_deformation(*ctx_.depsgraph, *object_);
    const OffsetIndices points_by_curve = curves_->points_by_curve();

    MutableSpan<float3> positions_cu = curves_->positions_for_write();
    const float3 brush_diff_cu = brush_end_cu - brush_start_cu;
    const float brush_radius_sq_cu = pow2f(brush_radius_cu);

    curve_selection_.foreach_segment(GrainSize(256), [&](const IndexMaskSegment segment) {
      MoveAndResampleBuffers resample_buffer;
      for (const int curve_i : segment) {
        const IndexRange points = points_by_curve[curve_i];
        const int last_point_i = points.last();
        const float3 old_pos_cu = deformation.positions[last_point_i];

        const float distance_to_brush_sq_cu = dist_squared_to_line_segment_v3(
            old_pos_cu, brush_start_cu, brush_end_cu);
        if (distance_to_brush_sq_cu > brush_radius_sq_cu) {
          continue;
        }

        const float distance_to_brush_cu = std::sqrt(distance_to_brush_sq_cu);

        const float radius_falloff = BKE_brush_curve_strength(
            brush_, distance_to_brush_cu, brush_radius_cu);
        const float weight = brush_strength_ * radius_falloff * curve_factors_[curve_i];

        const float3 translation_eval = weight * brush_diff_cu;
        const float3 translation_orig = deformation.translation_from_deformed_to_original(
            last_point_i, translation_eval);

        const float3 last_point_cu = positions_cu[last_point_i] + translation_orig;
        move_last_point_and_resample(resample_buffer, positions_cu.slice(points), last_point_cu);
      }
    });
  }
};

void SnakeHookOperation::on_stroke_extended(const bContext &C,
                                            const StrokeExtension &stroke_extension)
{
  SnakeHookOperatorExecutor executor{C};
  executor.execute(*this, C, stroke_extension);
}

std::unique_ptr<CurvesSculptStrokeOperation> new_snake_hook_operation()
{
  return std::make_unique<SnakeHookOperation>();
}

}  // namespace blender::ed::sculpt_paint

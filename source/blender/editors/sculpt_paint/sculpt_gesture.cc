/* SPDX-FileCopyrightText: 2024 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup edsculpt
 * Common helper methods and structures for gesture operations.
 */
#include "sculpt_gesture.hh"

#include "MEM_guardedalloc.h"

#include "DNA_vec_types.h"

#include "BLI_bitmap_draw_2d.h"
#include "BLI_lasso_2d.hh"
#include "BLI_math_geom.h"
#include "BLI_math_matrix.h"
#include "BLI_math_matrix.hh"
#include "BLI_math_matrix_types.hh"
#include "BLI_math_vector.h"
#include "BLI_math_vector.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_rect.h"

#include "BKE_context.hh"
#include "BKE_paint.hh"

#include "ED_view3d.hh"

#include "RNA_access.hh"
#include "RNA_define.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "paint_intern.hh"
#include "sculpt_intern.hh"

namespace blender::ed::sculpt_paint::gesture {

void operator_properties(wmOperatorType *ot, ShapeType shapeType)
{
  RNA_def_boolean(ot->srna,
                  "use_front_faces_only",
                  false,
                  "Front Faces Only",
                  "Affect only faces facing towards the view");

  if (shapeType == ShapeType::Line) {
    RNA_def_boolean(ot->srna,
                    "use_limit_to_segment",
                    false,
                    "Limit to Segment",
                    "Apply the gesture action only to the area that is contained within the "
                    "segment without extending its effect to the entire line");
  }
}

static void init_common(bContext *C, const wmOperator *op, GestureData &gesture_data)
{
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  gesture_data.vc = ED_view3d_viewcontext_init(C, depsgraph);
  const Object &object = *gesture_data.vc.obact;

  /* Operator properties. */
  gesture_data.front_faces_only = RNA_boolean_get(op->ptr, "use_front_faces_only");
  gesture_data.selection_type = SelectionType::Inside;

  /* SculptSession */
  gesture_data.ss = object.sculpt;

  /* Symmetry. */
  gesture_data.symm = ePaintSymmetryFlags(SCULPT_mesh_symmetry_xyz_get(object));

  /* View Normal. */
  const float3x3 view_inv(float4x4(gesture_data.vc.rv3d->viewinv));
  const float3 view_dir = math::transform_direction(view_inv, {0.0f, 0.0f, 1.0f});

  gesture_data.world_space_view_normal = math::normalize(view_dir);
  gesture_data.true_view_normal = math::normalize(
      math::transform_direction(object.world_to_object(), view_dir));

  /* View Origin. */
  gesture_data.world_space_view_origin = gesture_data.vc.rv3d->viewinv[3];
  gesture_data.true_view_origin = gesture_data.vc.rv3d->viewinv[3];
}

static void lasso_px_cb(const int x, const int x_end, const int y, void *user_data)
{
  GestureData *gesture_data = static_cast<GestureData *>(user_data);
  LassoData *lasso = &gesture_data->lasso;
  int index = (y * lasso->width) + x;
  const int index_end = (y * lasso->width) + x_end;
  do {
    lasso->mask_px[index].set();
  } while (++index != index_end);
}

std::unique_ptr<GestureData> init_from_polyline(bContext *C, wmOperator *op)
{
  return init_from_lasso(C, op);
}

std::unique_ptr<GestureData> init_from_lasso(bContext *C, wmOperator *op)
{
  const Array<int2> mcoords = WM_gesture_lasso_path_to_array(C, op);
  if (mcoords.size() <= 1) {
    return nullptr;
  }

  std::unique_ptr<GestureData> gesture_data = std::make_unique<GestureData>();
  gesture_data->shape_type = ShapeType::Lasso;

  init_common(C, op, *gesture_data);

  gesture_data->lasso.projviewobjmat = ED_view3d_ob_project_mat_get(gesture_data->vc.rv3d,
                                                                    gesture_data->vc.obact);
  BLI_lasso_boundbox(&gesture_data->lasso.boundbox, mcoords);
  const int lasso_width = 1 + gesture_data->lasso.boundbox.xmax -
                          gesture_data->lasso.boundbox.xmin;
  const int lasso_height = 1 + gesture_data->lasso.boundbox.ymax -
                           gesture_data->lasso.boundbox.ymin;
  gesture_data->lasso.width = lasso_width;
  gesture_data->lasso.mask_px.resize(lasso_width * lasso_height);

  BLI_bitmap_draw_2d_poly_v2i_n(gesture_data->lasso.boundbox.xmin,
                                gesture_data->lasso.boundbox.ymin,
                                gesture_data->lasso.boundbox.xmax,
                                gesture_data->lasso.boundbox.ymax,
                                mcoords,
                                lasso_px_cb,
                                gesture_data.get());

  BoundBox bb;
  ED_view3d_clipping_calc(&bb,
                          gesture_data->true_clip_planes,
                          gesture_data->vc.region,
                          gesture_data->vc.obact,
                          &gesture_data->lasso.boundbox);

  gesture_data->gesture_points.reinitialize(mcoords.size());
  for (const int i : mcoords.index_range()) {
    gesture_data->gesture_points[i][0] = mcoords[i][0];
    gesture_data->gesture_points[i][1] = mcoords[i][1];
  }

  return gesture_data;
}

std::unique_ptr<GestureData> init_from_box(bContext *C, wmOperator *op)
{
  std::unique_ptr<GestureData> gesture_data = std::make_unique<GestureData>();
  gesture_data->shape_type = ShapeType::Box;

  init_common(C, op, *gesture_data);

  rcti rect;
  WM_operator_properties_border_to_rcti(op, &rect);

  BoundBox bb;
  ED_view3d_clipping_calc(
      &bb, gesture_data->true_clip_planes, gesture_data->vc.region, gesture_data->vc.obact, &rect);

  gesture_data->gesture_points.reinitialize(4);

  gesture_data->gesture_points[0][0] = rect.xmax;
  gesture_data->gesture_points[0][1] = rect.ymax;

  gesture_data->gesture_points[1][0] = rect.xmax;
  gesture_data->gesture_points[1][1] = rect.ymin;

  gesture_data->gesture_points[2][0] = rect.xmin;
  gesture_data->gesture_points[2][1] = rect.ymin;

  gesture_data->gesture_points[3][0] = rect.xmin;
  gesture_data->gesture_points[3][1] = rect.ymax;
  return gesture_data;
}

static void line_plane_from_tri(float *r_plane,
                                const GestureData &gesture_data,
                                const bool flip,
                                const float3 &p1,
                                const float3 &p2,
                                const float3 &p3)
{
  float3 normal;
  normal_tri_v3(normal, p1, p2, p3);
  normal = math::normalize(
      math::transform_direction(gesture_data.vc.obact->world_to_object(), normal));
  if (flip) {
    normal *= -1.0f;
  }
  const float3 plane_point_object_space = math::transform_point(
      gesture_data.vc.obact->world_to_object(), p1);
  plane_from_point_normal_v3(r_plane, plane_point_object_space, normal);
}

/* Creates 4 points in the plane defined by the line and 2 extra points with an offset relative to
 * this plane. */
static void line_calculate_plane_points(const GestureData &gesture_data,
                                        const Span<float2> line_points,
                                        std::array<float3, 4> &r_plane_points,
                                        std::array<float3, 2> &r_offset_plane_points)
{
  const float3 depth_point = gesture_data.true_view_origin + gesture_data.true_view_normal;
  ED_view3d_win_to_3d(
      gesture_data.vc.v3d, gesture_data.vc.region, depth_point, line_points[0], r_plane_points[0]);
  ED_view3d_win_to_3d(
      gesture_data.vc.v3d, gesture_data.vc.region, depth_point, line_points[1], r_plane_points[3]);

  const float3 offset_depth_point = gesture_data.true_view_origin +
                                    gesture_data.true_view_normal * 10.0f;
  ED_view3d_win_to_3d(gesture_data.vc.v3d,
                      gesture_data.vc.region,
                      offset_depth_point,
                      line_points[0],
                      r_plane_points[1]);
  ED_view3d_win_to_3d(gesture_data.vc.v3d,
                      gesture_data.vc.region,
                      offset_depth_point,
                      line_points[1],
                      r_plane_points[2]);

  float3 normal;
  normal_tri_v3(normal, r_plane_points[0], r_plane_points[1], r_plane_points[2]);
  r_offset_plane_points[0] = r_plane_points[0] + normal;
  r_offset_plane_points[1] = r_plane_points[3] + normal;
}

std::unique_ptr<GestureData> init_from_line(bContext *C, const wmOperator *op)
{
  std::unique_ptr<GestureData> gesture_data = std::make_unique<GestureData>();
  gesture_data->shape_type = ShapeType::Line;
  gesture_data->line.use_side_planes = RNA_boolean_get(op->ptr, "use_limit_to_segment");

  init_common(C, op, *gesture_data);

  gesture_data->gesture_points.reinitialize(2);
  gesture_data->gesture_points[0] = {float(RNA_int_get(op->ptr, "xstart")),
                                     float(RNA_int_get(op->ptr, "ystart"))};
  gesture_data->gesture_points[1] = {float(RNA_int_get(op->ptr, "xend")),
                                     float(RNA_int_get(op->ptr, "yend"))};

  gesture_data->line.flip = RNA_boolean_get(op->ptr, "flip");

  std::array<float3, 4> plane_points;
  std::array<float3, 2> offset_plane_points;
  line_calculate_plane_points(
      *gesture_data, gesture_data->gesture_points, plane_points, offset_plane_points);

  /* Calculate line plane and normal. */
  const bool flip = gesture_data->line.flip ^ (!gesture_data->vc.rv3d->is_persp);
  line_plane_from_tri(gesture_data->line.true_plane,
                      *gesture_data,
                      flip,
                      plane_points[0],
                      plane_points[1],
                      plane_points[2]);

  /* Calculate the side planes. */
  line_plane_from_tri(gesture_data->line.true_side_plane[0],
                      *gesture_data,
                      false,
                      plane_points[1],
                      plane_points[0],
                      offset_plane_points[0]);
  line_plane_from_tri(gesture_data->line.true_side_plane[1],
                      *gesture_data,
                      false,
                      plane_points[3],
                      plane_points[2],
                      offset_plane_points[1]);

  return gesture_data;
}

GestureData::~GestureData()
{
  MEM_SAFE_FREE(this->operation);
}

static void flip_plane(float out[4], const float in[4], const char symm)
{
  if (symm & PAINT_SYMM_X) {
    out[0] = -in[0];
  }
  else {
    out[0] = in[0];
  }
  if (symm & PAINT_SYMM_Y) {
    out[1] = -in[1];
  }
  else {
    out[1] = in[1];
  }
  if (symm & PAINT_SYMM_Z) {
    out[2] = -in[2];
  }
  else {
    out[2] = in[2];
  }

  out[3] = in[3];
}

static void flip_for_symmetry_pass(GestureData &gesture_data, const ePaintSymmetryFlags symmpass)
{
  gesture_data.symmpass = symmpass;
  for (int j = 0; j < 4; j++) {
    flip_plane(gesture_data.clip_planes[j], gesture_data.true_clip_planes[j], symmpass);
  }

  negate_m4(gesture_data.clip_planes);

  gesture_data.view_normal = symmetry_flip(gesture_data.true_view_normal, symmpass);
  gesture_data.view_origin = symmetry_flip(gesture_data.true_view_origin, symmpass);
  flip_plane(gesture_data.line.plane, gesture_data.line.true_plane, symmpass);
  flip_plane(gesture_data.line.side_plane[0], gesture_data.line.true_side_plane[0], symmpass);
  flip_plane(gesture_data.line.side_plane[1], gesture_data.line.true_side_plane[1], symmpass);
}

static void update_affected_nodes_by_line_plane(GestureData &gesture_data)
{
  const bke::pbvh::Tree &pbvh = *bke::object::pbvh_get(*gesture_data.vc.obact);
  std::array<float4, 3> clip_planes;
  copy_v4_v4(clip_planes[0], gesture_data.line.plane);
  copy_v4_v4(clip_planes[1], gesture_data.line.side_plane[0]);
  copy_v4_v4(clip_planes[2], gesture_data.line.side_plane[1]);

  gesture_data.node_mask = bke::pbvh::search_nodes(
      pbvh, gesture_data.node_mask_memory, [&](const bke::pbvh::Node &node) {
        return bke::pbvh::node_frustum_contain_aabb(
            node, Span(clip_planes).take_front(gesture_data.line.use_side_planes ? 3 : 1));
      });
}

static void update_affected_nodes_by_clip_planes(GestureData &gesture_data)
{
  const bke::pbvh::Tree &pbvh = *bke::object::pbvh_get(*gesture_data.vc.obact);
  float clip_planes[4][4];
  copy_m4_m4(clip_planes, gesture_data.clip_planes);
  negate_m4(clip_planes);

  Span planes(reinterpret_cast<float4 *>(clip_planes), 4);

  gesture_data.node_mask = bke::pbvh::search_nodes(
      pbvh, gesture_data.node_mask_memory, [&](const bke::pbvh::Node &node) {
        switch (gesture_data.selection_type) {
          case SelectionType::Inside:
            return bke::pbvh::node_frustum_contain_aabb(node, planes);
          case SelectionType::Outside:
            /* Certain degenerate cases of a lasso shape can cause the resulting
             * frustum planes to enclose a node's AABB, therefore we must submit it
             * to be more thoroughly evaluated. */
            if (gesture_data.shape_type == ShapeType::Lasso) {
              return true;
            }
            return bke::pbvh::node_frustum_exclude_aabb(node, planes);
        }
        BLI_assert_unreachable();
        return false;
      });
}

static void update_affected_nodes(GestureData &gesture_data)
{
  switch (gesture_data.shape_type) {
    case ShapeType::Box:
    case ShapeType::Lasso:
      update_affected_nodes_by_clip_planes(gesture_data);
      break;
    case ShapeType::Line:
      update_affected_nodes_by_line_plane(gesture_data);
      break;
  }
}

static bool is_affected_lasso(const GestureData &gesture_data, const float3 &position)
{
  const float3 co_final = symmetry_flip(position, gesture_data.symmpass);

  /* First project point to 2d space. */
  const float2 scr_co_f = ED_view3d_project_float_v2_m4(
      gesture_data.vc.region, co_final, gesture_data.lasso.projviewobjmat);

  int2 screen_coords = {int(scr_co_f[0]), int(scr_co_f[1])};

  /* Clip against lasso boundbox. */
  const LassoData &lasso = gesture_data.lasso;
  if (!BLI_rcti_isect_pt_v(&lasso.boundbox, screen_coords)) {
    return gesture_data.selection_type == SelectionType::Outside;
  }

  screen_coords[0] -= lasso.boundbox.xmin;
  screen_coords[1] -= lasso.boundbox.ymin;

  const bool bitmap_result =
      lasso.mask_px[screen_coords[1] * lasso.width + screen_coords[0]].test();
  switch (gesture_data.selection_type) {
    case SelectionType::Inside:
      return bitmap_result;
    case SelectionType::Outside:
      return !bitmap_result;
  }
  BLI_assert_unreachable();
  return false;
}

bool is_affected(const GestureData &gesture_data, const float3 &position, const float3 &normal)
{
  const float dot = math::dot(gesture_data.view_normal, normal);
  const bool is_affected_front_face = !(gesture_data.front_faces_only && dot < 0.0f);

  if (!is_affected_front_face) {
    return false;
  }

  switch (gesture_data.shape_type) {
    case ShapeType::Box: {
      const bool is_contained = isect_point_planes_v3(gesture_data.clip_planes, 4, position);
      return ((is_contained && gesture_data.selection_type == SelectionType::Inside) ||
              (!is_contained && gesture_data.selection_type == SelectionType::Outside));
    }
    case ShapeType::Lasso:
      return is_affected_lasso(gesture_data, position);
    case ShapeType::Line:
      if (gesture_data.line.use_side_planes) {
        return plane_point_side_v3(gesture_data.line.plane, position) > 0.0f &&
               plane_point_side_v3(gesture_data.line.side_plane[0], position) > 0.0f &&
               plane_point_side_v3(gesture_data.line.side_plane[1], position) > 0.0f;
      }
      return plane_point_side_v3(gesture_data.line.plane, position) > 0.0f;
  }
  return false;
}

void filter_factors(const GestureData &gesture_data,
                    const Span<float3> positions,
                    const Span<float3> normals,
                    const MutableSpan<float> factors)
{
  for (const int i : positions.index_range()) {
    if (!is_affected(gesture_data, positions[i], normals[i])) {
      factors[i] = 0.0f;
    }
  }
}

void apply(bContext &C, GestureData &gesture_data, wmOperator &op)
{
  const Operation *operation = gesture_data.operation;

  operation->begin(C, op, gesture_data);

  for (int symmpass = 0; symmpass <= gesture_data.symm; symmpass++) {
    if (is_symmetry_iteration_valid(symmpass, gesture_data.symm)) {
      flip_for_symmetry_pass(gesture_data, ePaintSymmetryFlags(symmpass));
      update_affected_nodes(gesture_data);

      operation->apply_for_symmetry_pass(C, gesture_data);
    }
  }

  operation->end(C, gesture_data);

  SCULPT_tag_update_overlays(&C);
}
}  // namespace blender::ed::sculpt_paint::gesture

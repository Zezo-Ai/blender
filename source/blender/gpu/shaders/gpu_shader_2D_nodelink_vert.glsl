/* SPDX-FileCopyrightText: 2018-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * 2D Cubic Bezier thick line drawing
 */

/**
 * `uv.x` is position along the curve, defining the tangent space.
 * `uv.y` is "signed" distance (compressed to [0..1] range) from the pos in expand direction
 * `pos` is the verts position in the curve tangent space
 */

#include "gpu_shader_math_vector_lib.glsl"
#include "infos/gpu_shader_2D_nodelink_info.hh"

VERTEX_SHADER_CREATE_INFO(gpu_shader_2D_nodelink)

#define MID_VERTEX 65

void main()
{
  constexpr float start_gradient_threshold = 0.35f;
  constexpr float end_gradient_threshold = 0.65f;

#ifdef USE_INSTANCE
#  define colStart (colid_doarrow[0] < 3u ? start_color : node_link_data.colors[colid_doarrow[0]])
#  define colEnd (colid_doarrow[1] < 3u ? end_color : node_link_data.colors[colid_doarrow[1]])
#  define colShadow node_link_data.colors[colid_doarrow[2]]
#  define doArrow (colid_doarrow[3] != 0u)
#  define doMuted (domuted[0] != 0)
#else
  float2 P0 = node_link_data.bezierPts[0].xy;
  float2 P1 = node_link_data.bezierPts[1].xy;
  float2 P2 = node_link_data.bezierPts[2].xy;
  float2 P3 = node_link_data.bezierPts[3].xy;
  bool doArrow = node_link_data.doArrow;
  bool doMuted = node_link_data.doMuted;
  float dim_factor = node_link_data.dim_factor;
  float thickness = node_link_data.thickness;
  float3 dash_params = node_link_data.dash_params.xyz;
  int has_back_link = node_link_data.has_back_link ? 1 : 0;

  float4 colShadow = node_link_data.colors[0];
  float4 colStart = node_link_data.colors[1];
  float4 colEnd = node_link_data.colors[2];
#endif

  float line_thickness = thickness;
  bool is_outline_pass = gl_VertexID < MID_VERTEX;
  isMainLine = expand.y == 1.0f && !is_outline_pass ? 1 : 0;

  if ((expand.y == 1.0f) && has_back_link != 0) {
    /* Increase width because two links are drawn. */
    line_thickness *= 1.7f;
  }

  if (is_outline_pass) {
    /* Outline pass. */
    finalColor = colShadow;
  }
  else {
    /* Second pass. */
    if (uv.x < start_gradient_threshold) {
      finalColor = colStart;
    }
    else if (uv.x > end_gradient_threshold) {
      finalColor = colEnd;
    }
    else {
      float mixFactor = (uv.x - start_gradient_threshold) /
                        (end_gradient_threshold - start_gradient_threshold);
      finalColor = mix(colStart, colEnd, mixFactor);
    }
    line_thickness *= 0.65f;
    if (doMuted) {
      finalColor[3] = 0.65f;
    }
  }

  aspect = node_link_data.aspect;
  /* Parameters for the dashed line. */
  dashLength = dash_params.x;
  dashFactor = dash_params.y;
  dashAlpha = dash_params.z;
  /* Approximate line length, no need for real bezier length calculation. */
  lineLength = distance(P0, P3);
  /* TODO: Incorrect U, this leads to non-uniform dash distribution. */
  lineUV = uv;
  hasBackLink = has_back_link;

  float t = uv.x;
  float t2 = t * t;
  float t2_3 = 3.0f * t2;
  float one_minus_t = 1.0f - t;
  float one_minus_t2 = one_minus_t * one_minus_t;
  float one_minus_t2_3 = 3.0f * one_minus_t2;

  float2 point = (P0 * one_minus_t2 * one_minus_t + P1 * one_minus_t2_3 * t +
                  P2 * t2_3 * one_minus_t + P3 * t2 * t);

  float2 tangent = ((P1 - P0) * one_minus_t2_3 + (P2 - P1) * 6.0f * (t - t2) + (P3 - P2) * t2_3);

  /* Tangent space at t. If the inner and outer control points overlap, the tangent is invalid.
   * Use the vector between the sockets instead. */
  tangent = is_zero(tangent) ? normalize(P3 - P0) : normalize(tangent);
  float2 normal = tangent.yx * float2(-1.0f, 1.0f);

  /* Position vertex on the curve tangent space */
  point += (pos.x * tangent + pos.y * normal) * node_link_data.arrowSize;

  gl_Position = ModelViewProjectionMatrix * float4(point, 0.0f, 1.0f);

  float2 exp_axis = expand.x * tangent + expand.y * normal;

  /* rotate & scale the expand axis */
  exp_axis = ModelViewProjectionMatrix[0].xy * exp_axis.xx +
             ModelViewProjectionMatrix[1].xy * exp_axis.yy;

  float expand_dist = line_thickness * (uv.y * 2.0f - 1.0f);
  lineThickness = line_thickness;

  finalColor[3] *= dim_factor;

  /* Expand into a line */
  gl_Position.xy += exp_axis * node_link_data.aspect * expand_dist;

  /* If the link is not muted or is not a reroute arrow the points are squashed to the center of
   * the line. Magic numbers are defined in `drawnode.cc`. */
  if ((expand.x == 1.0f && !doMuted) ||
      (expand.y != 1.0f && (pos.x < 0.70f || pos.x > 0.71f) && !doArrow))
  {
    gl_Position.xy *= 0.0f;
  }
}

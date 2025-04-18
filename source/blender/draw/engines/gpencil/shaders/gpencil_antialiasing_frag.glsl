/* SPDX-FileCopyrightText: 2020-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/gpencil_info.hh"

FRAGMENT_SHADER_CREATE_INFO(gpencil_antialiasing_stage_1)

#include "gpu_shader_smaa_lib.glsl"

void main()
{
#if SMAA_STAGE == 0
  /* Detect edges in color and revealage buffer. */
  out_edges = SMAALumaEdgeDetectionPS(uvs, offset, colorTex);
  out_edges = max(out_edges, SMAALumaEdgeDetectionPS(uvs, offset, revealTex));
  /* Discard if there is no edge. */
  if (dot(out_edges, float2(1.0f, 1.0f)) == 0.0f) {
    discard;
    return;
  }

#elif SMAA_STAGE == 1
  out_weights = SMAABlendingWeightCalculationPS(
      uvs, pixcoord, offset, edgesTex, areaTex, searchTex, float4(0));

#elif SMAA_STAGE == 2
  /* Resolve both buffers. */
  if (doAntiAliasing) {
    out_color = SMAANeighborhoodBlendingPS(uvs, offset[0], colorTex, blendTex);
    out_reveal = SMAANeighborhoodBlendingPS(uvs, offset[0], revealTex, blendTex);
  }
  else {
    out_color = texture(colorTex, uvs);
    out_reveal = texture(revealTex, uvs);
  }

  /* Revealage, how much light passes through. */
  /* Average for alpha channel. */
  out_reveal.a = clamp(dot(out_reveal.rgb, float3(0.333334f)), 0.0f, 1.0f);
  /* Color buffer is already pre-multiplied. Just add it to the color. */
  /* Add the alpha. */
  out_color.a = 1.0f - out_reveal.a;

  if (onlyAlpha) {
    /* Special case in wire-frame X-ray mode. */
    out_color = float4(0.0f);
    out_reveal.rgb = out_reveal.aaa;
  }
#endif
}

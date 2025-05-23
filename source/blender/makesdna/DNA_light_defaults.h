/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup DNA
 */

#pragma once

/* clang-format off */

/* -------------------------------------------------------------------- */
/** \name Light Struct
 * \{ */

#define _DNA_DEFAULT_Light \
  { \
    .r = 1.0f, \
    .g = 1.0f, \
    .b = 1.0f, \
    .temperature = 6500.0f, \
    .energy = 10.0f, \
    .energy_deprecated = 10.0f, \
    .spotsize = DEG2RADF(45.0f), \
    .spotblend = 0.15f, \
    .mode = LA_SHADOW | LA_USE_SOFT_FALLOFF, \
    .clipsta = 0.05f, \
    .area_size = 0.25f, \
    .area_sizey = 0.25f, \
    .area_sizez = 0.25f, \
    .preview = NULL, \
    .cascade_max_dist = 200.0f, \
    .cascade_count = 4, \
    .cascade_exponent = 0.8f, \
    .cascade_fade = 0.1f, \
    .diff_fac = 1.0f, \
    .spec_fac = 1.0f, \
    .transmission_fac = 1.0f, \
    .volume_fac = 1.0f, \
    .shadow_filter_radius = 1.0f, \
    .shadow_maximum_resolution = 0.001f, \
    .shadow_jitter_overblur = 10.0f, \
    .att_dist = 40.0f, \
    .sun_angle = DEG2RADF(0.526f), \
    .area_spread = DEG2RADF(180.0f), \
  }

/** \} */

/* clang-format on */

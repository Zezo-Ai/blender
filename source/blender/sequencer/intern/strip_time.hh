/* SPDX-FileCopyrightText: 2004 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "BLI_span.hh"

/** \file
 * \ingroup sequencer
 */

struct ListBase;
struct Scene;
struct Strip;

namespace blender::seq {

void strip_update_sound_bounds_recursive(const Scene *scene, Strip *strip_meta);

/* Describes gap between strips in timeline. */
struct GapInfo {
  int gap_start_frame; /* Start frame of the gap. */
  int gap_length;      /* Length of the gap. */
  bool gap_exists;     /* False if there are no gaps. */
};

/**
 * Find first gap between strips after initial_frame and describe it by filling data of r_gap_info
 *
 * \param scene: Scene in which strips are located.
 * \param seqbase: ListBase in which strips are located.
 * \param initial_frame: frame on timeline from where gaps are searched for.
 * \param r_gap_info: data structure describing gap, that will be filled in by this function.
 */
void seq_time_gap_info_get(const Scene *scene,
                           ListBase *seqbase,
                           int initial_frame,
                           GapInfo *r_gap_info);
void strip_time_effect_range_set(const Scene *scene, Strip *strip);
/**
 * Update strip `startdisp` and `enddisp` (n-input effects have no length to calculate these).
 */
void strip_time_update_effects_strip_range(const Scene *scene, blender::Span<Strip *> effects);
void strip_time_translate_handles(const Scene *scene, Strip *strip, const int offset);
float strip_time_media_playback_rate_factor_get(const Scene *scene, const Strip *strip);
float strip_retiming_evaluate(const Strip *strip, const float frame_index);

}  // namespace blender::seq

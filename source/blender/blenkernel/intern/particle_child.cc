/* SPDX-FileCopyrightText: Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup bke
 */

#include "BLI_math_matrix.h"
#include "BLI_math_rotation.h"
#include "BLI_math_vector.h"
#include "BLI_noise.h"

#include "DNA_material_types.h"
#include "DNA_object_types.h"

#include "BKE_colortools.hh"
#include "BKE_particle.h"

#include "particle_private.h"

/* ------------------------------------------------------------------------- */

struct ParticlePathIterator {
  ParticleCacheKey *key;
  int index;
  float time;

  ParticleCacheKey *parent_key;
  float parent_rotation[4];
};

static void psys_path_iter_get(ParticlePathIterator *iter,
                               ParticleCacheKey *keys,
                               int totkeys,
                               ParticleCacheKey *parent,
                               int index)
{
  BLI_assert(index >= 0 && index < totkeys);

  iter->key = keys + index;
  iter->index = index;
  iter->time = float(index) / float(totkeys - 1);

  if (parent) {
    iter->parent_key = parent + index;
    if (index > 0) {
      mul_qt_qtqt(iter->parent_rotation, iter->parent_key->rot, parent->rot);
    }
    else {
      copy_qt_qt(iter->parent_rotation, parent->rot);
    }
  }
  else {
    iter->parent_key = nullptr;
    unit_qt(iter->parent_rotation);
  }
}

/* ------------------------------------------------------------------------- */

static void do_kink_spiral_deform(ParticleKey *state,
                                  const float dir[3],
                                  const float kink[3],
                                  float time,
                                  float freq,
                                  float shape,
                                  float amplitude,
                                  const float spiral_start[3])
{
  float result[3];

  CLAMP(time, 0.0f, 1.0f);

  copy_v3_v3(result, state->co);

  {
    /* Creates a logarithmic spiral:
     *   r(theta) = a * exp(b * theta)
     *
     * The "density" parameter b is defined by the shape parameter
     * and goes up to the Golden Spiral for 1.0
     * https://en.wikipedia.org/wiki/Golden_spiral
     */
    const float b = shape * (1.0f + sqrtf(5.0f)) / float(M_PI) * 0.25f;
    /* angle of the spiral against the curve (rotated opposite to make a smooth transition) */
    const float start_angle = ((b != 0.0f) ? atanf(1.0f / b) : float(-M_PI_2)) +
                              (b > 0.0f ? -float(M_PI_2) : float(M_PI_2));

    float spiral_axis[3], rot[3][3];
    float vec[3];

    float theta = freq * time * 2.0f * float(M_PI);
    float radius = amplitude * expf(b * theta);

    /* a bit more intuitive than using negative frequency for this */
    if (amplitude < 0.0f) {
      theta = -theta;
    }

    cross_v3_v3v3(spiral_axis, dir, kink);
    normalize_v3(spiral_axis);

    mul_v3_v3fl(vec, kink, -radius);

    axis_angle_normalized_to_mat3(rot, spiral_axis, theta);
    mul_m3_v3(rot, vec);

    madd_v3_v3fl(vec, kink, amplitude);

    axis_angle_normalized_to_mat3(rot, spiral_axis, -start_angle);
    mul_m3_v3(rot, vec);

    add_v3_v3v3(result, spiral_start, vec);
  }

  copy_v3_v3(state->co, result);
}

static void do_kink_spiral(ParticleThreadContext *ctx,
                           ParticleTexture *ptex,
                           const float parent_orco[3],
                           ChildParticle *cpa,
                           const float orco[3],
                           float hairmat[4][4],
                           ParticleCacheKey *keys,
                           ParticleCacheKey *parent_keys,
                           int *r_totkeys,
                           float *r_max_length)
{
  ParticleSettings *part = ctx->sim.psys->part;
  const int seed = ctx->sim.psys->child_seed + int(cpa - ctx->sim.psys->child);
  const int totkeys = ctx->segments + 1;
  const int extrakeys = ctx->extra_segments;

  float kink_amp_random = part->kink_amp_random;
  float kink_amp = part->kink_amp *
                   (1.0f - kink_amp_random * psys_frand(ctx->sim.psys, 93541 + seed));
  float kink_freq = part->kink_freq;
  float kink_shape = part->kink_shape;
  float kink_axis_random = part->kink_axis_random;

  ParticlePathIterator iter;
  ParticleCacheKey *key;
  int k;

  float dir[3];
  float spiral_start[3] = {0.0f, 0.0f, 0.0f};
  float spiral_start_time = 0.0f;
  float spiral_par_co[3] = {0.0f, 0.0f, 0.0f};
  float spiral_par_vel[3] = {0.0f, 0.0f, 0.0f};
  float spiral_par_rot[4] = {1.0f, 0.0f, 0.0f, 0.0f};
  float totlen;
  float cut_time;
  int start_index = 0, end_index = 0;
  float kink_base[3];

  kink_amp *= ptex->kink_amp;
  kink_freq *= ptex->kink_freq;

  cut_time = (totkeys - 1) * ptex->length;
  zero_v3(spiral_start);

  for (k = 0, key = keys; k < totkeys - 1; k++, key++) {
    if (float(k + 1) >= cut_time) {
      float fac = cut_time - float(k);
      ParticleCacheKey *par = parent_keys + k;

      start_index = k + 1;
      end_index = start_index + extrakeys;

      spiral_start_time = (float(k) + fac) / float(totkeys - 1);
      interp_v3_v3v3(spiral_start, key->co, (key + 1)->co, fac);

      interp_v3_v3v3(spiral_par_co, par->co, (par + 1)->co, fac);
      interp_v3_v3v3(spiral_par_vel, par->vel, (par + 1)->vel, fac);
      interp_qt_qtqt(spiral_par_rot, par->rot, (par + 1)->rot, fac);

      break;
    }
  }

  zero_v3(dir);

  zero_v3(kink_base);
  kink_base[part->kink_axis] = 1.0f;
  mul_mat3_m4_v3(ctx->sim.ob->object_to_world().ptr(), kink_base);

  /* Fill in invariant part of modifier context. */
  ParticleChildModifierContext modifier_ctx = {nullptr};
  modifier_ctx.thread_ctx = ctx;
  modifier_ctx.sim = &ctx->sim;
  modifier_ctx.ptex = ptex;
  modifier_ctx.cpa = cpa;
  modifier_ctx.orco = orco;
  modifier_ctx.parent_keys = parent_keys;

  for (k = 0, key = keys; k < end_index; k++, key++) {
    float par_time;
    float *par_co, *par_vel, *par_rot;

    psys_path_iter_get(&iter, keys, end_index, nullptr, k);
    if (k < start_index) {
      sub_v3_v3v3(dir, (key + 1)->co, key->co);
      normalize_v3(dir);

      par_time = float(k) / float(totkeys - 1);
      par_co = parent_keys[k].co;
      par_vel = parent_keys[k].vel;
      par_rot = parent_keys[k].rot;
    }
    else {
      float spiral_time = float(k - start_index) / float(extrakeys - 1);
      float kink[3], tmp[3];

      /* use same time value for every point on the spiral */
      par_time = spiral_start_time;
      par_co = spiral_par_co;
      par_vel = spiral_par_vel;
      par_rot = spiral_par_rot;

      project_v3_v3v3(tmp, kink_base, dir);
      sub_v3_v3v3(kink, kink_base, tmp);
      normalize_v3(kink);

      if (kink_axis_random > 0.0f) {
        float a = kink_axis_random * (psys_frand(ctx->sim.psys, 7112 + seed) * 2.0f - 1.0f) *
                  float(M_PI);
        float rot[3][3];

        axis_angle_normalized_to_mat3(rot, dir, a);
        mul_m3_v3(rot, kink);
      }

      do_kink_spiral_deform((ParticleKey *)key,
                            dir,
                            kink,
                            spiral_time,
                            kink_freq,
                            kink_shape,
                            kink_amp,
                            spiral_start);
    }

    /* Fill in variant part of modifier context. */
    modifier_ctx.par_co = par_co;
    modifier_ctx.par_vel = par_vel;
    modifier_ctx.par_rot = par_rot;
    modifier_ctx.par_orco = parent_orco;

    /* Apply different deformations to the child path/ */
    do_child_modifiers(&modifier_ctx, hairmat, (ParticleKey *)key, par_time);
  }

  totlen = 0.0f;
  for (k = 0, key = keys; k < end_index - 1; k++, key++) {
    totlen += len_v3v3((key + 1)->co, key->co);
  }

  *r_totkeys = end_index;
  *r_max_length = totlen;
}

/* ------------------------------------------------------------------------- */

static bool check_path_length(int k,
                              ParticleCacheKey *keys,
                              ParticleCacheKey *key,
                              float max_length,
                              float step_length,
                              float *cur_length,
                              float dvec[3])
{
  if (*cur_length + step_length > max_length) {
    sub_v3_v3v3(dvec, key->co, (key - 1)->co);
    mul_v3_fl(dvec, (max_length - *cur_length) / step_length);
    add_v3_v3v3(key->co, (key - 1)->co, dvec);
    keys->segments = k;
    /* something over the maximum step value */
    return false;
  }

  *cur_length += step_length;
  return true;
}

void psys_apply_child_modifiers(ParticleThreadContext *ctx,
                                ListBase * /*modifiers*/,
                                ChildParticle *cpa,
                                ParticleTexture *ptex,
                                const float orco[3],
                                float hairmat[4][4],
                                ParticleCacheKey *keys,
                                ParticleCacheKey *parent_keys,
                                const float parent_orco[3])
{
  ParticleSettings *part = ctx->sim.psys->part;
  Material *ma = ctx->ma;
  const bool draw_col_ma = (part->draw_col == PART_DRAW_COL_MAT);
  const bool use_length_check = !ELEM(part->kink, PART_KINK_SPIRAL);

  ParticleCacheKey *key;
  int totkeys, k;
  float max_length;

  /* TODO: for the future: use true particle modifiers that work on the whole curve. */

  if (part->kink == PART_KINK_SPIRAL) {
    do_kink_spiral(
        ctx, ptex, parent_orco, cpa, orco, hairmat, keys, parent_keys, &totkeys, &max_length);
    keys->segments = totkeys - 1;
  }
  else {
    /* Fill in invariant part of modifier context. */
    ParticleChildModifierContext modifier_ctx = {nullptr};
    modifier_ctx.thread_ctx = ctx;
    modifier_ctx.sim = &ctx->sim;
    modifier_ctx.ptex = ptex;
    modifier_ctx.cpa = cpa;
    modifier_ctx.orco = orco;
    modifier_ctx.parent_keys = parent_keys;

    totkeys = ctx->segments + 1;
    max_length = ptex->length;

    for (k = 0, key = keys; k < totkeys; k++, key++) {
      ParticlePathIterator iter;
      psys_path_iter_get(&iter, keys, totkeys, parent_keys, k);

      ParticleKey *par = (ParticleKey *)iter.parent_key;

      /* Fill in variant part of modifier context. */
      modifier_ctx.par_co = par->co;
      modifier_ctx.par_vel = par->vel;
      modifier_ctx.par_rot = iter.parent_rotation;
      modifier_ctx.par_orco = parent_orco;

      /* Apply different deformations to the child path. */
      do_child_modifiers(&modifier_ctx, hairmat, (ParticleKey *)key, iter.time);
    }
  }

  {
    const float step_length = 1.0f / float(totkeys - 1);
    float cur_length = 0.0f;

    if (max_length <= 0.0f) {
      keys->segments = -1;
      totkeys = 0;
    }

    /* we have to correct velocity because of kink & clump */
    for (k = 0, key = keys; k < totkeys; k++, key++) {
      if (k >= 2) {
        sub_v3_v3v3((key - 1)->vel, key->co, (key - 2)->co);
        mul_v3_fl((key - 1)->vel, 0.5);
      }

      if (use_length_check && k > 0) {
        float dvec[3];
        /* check if path needs to be cut before actual end of data points */
        if (!check_path_length(k, keys, key, max_length, step_length, &cur_length, dvec)) {
          /* last key */
          sub_v3_v3v3(key->vel, key->co, (key - 1)->co);
          if (ma && draw_col_ma) {
            copy_v3_v3(key->col, &ma->r);
          }
          break;
        }
      }
      if (k == totkeys - 1) {
        /* last key */
        sub_v3_v3v3(key->vel, key->co, (key - 1)->co);
      }

      if (ma && draw_col_ma) {
        copy_v3_v3(key->col, &ma->r);
      }
    }
  }
}

/* ------------------------------------------------------------------------- */

void do_kink(ParticleKey *state,
             const float par_co[3],
             const float par_vel[3],
             const float par_rot[4],
             float time,
             float freq,
             float shape,
             float amplitude,
             float flat,
             short type,
             short axis,
             const float obmat[4][4],
             int smooth_start)
{
  float kink[3] = {1.0f, 0.0f, 0.0f}, par_vec[3];
  float t, dt = 1.0f, result[3];

  if (ELEM(type, PART_KINK_NO, PART_KINK_SPIRAL)) {
    return;
  }

  CLAMP(time, 0.0f, 1.0f);

  if (shape != 0.0f && !ELEM(type, PART_KINK_BRAID)) {
    if (shape < 0.0f) {
      time = pow(time, 1.0f + shape);
    }
    else {
      time = pow(time, 1.0f / (1.0f - shape));
    }
  }

  t = time * freq * float(M_PI);

  if (smooth_start) {
    dt = fabsf(t);
    /* smooth the beginning of kink */
    CLAMP(dt, 0.0f, float(M_PI));
    dt = sinf(dt / 2.0f);
  }

  if (!ELEM(type, PART_KINK_RADIAL)) {
    float temp[3];

    kink[axis] = 1.0f;

    if (obmat) {
      mul_mat3_m4_v3(obmat, kink);
    }

    mul_qt_v3(par_rot, kink);

    /* make sure kink is normal to strand */
    project_v3_v3v3(temp, kink, par_vel);
    sub_v3_v3(kink, temp);
    normalize_v3(kink);
  }

  copy_v3_v3(result, state->co);
  sub_v3_v3v3(par_vec, par_co, state->co);

  switch (type) {
    case PART_KINK_CURL: {
      float curl_offset[3];
      float q1[4] = {1.0f, 0.0f, 0.0f, 0.0f};

      /* rotate kink vector around strand tangent */
      mul_v3_v3fl(curl_offset, kink, amplitude);
      axis_angle_to_quat(q1, par_vel, t);
      mul_qt_v3(q1, curl_offset);

      interp_v3_v3v3(par_vec, state->co, par_co, flat);
      add_v3_v3v3(result, par_vec, curl_offset);
      break;
    }
    case PART_KINK_RADIAL: {
      if (flat > 0.0f) {
        float proj[3];
        /* flatten along strand */
        project_v3_v3v3(proj, par_vec, par_vel);
        madd_v3_v3fl(result, proj, flat);
      }

      madd_v3_v3fl(result, par_vec, -amplitude * sinf(t));
      break;
    }
    case PART_KINK_WAVE: {
      madd_v3_v3fl(result, kink, amplitude * sinf(t));

      if (flat > 0.0f) {
        float proj[3];
        /* flatten along wave */
        project_v3_v3v3(proj, par_vec, kink);
        madd_v3_v3fl(result, proj, flat);

        /* flatten along strand */
        project_v3_v3v3(proj, par_vec, par_vel);
        madd_v3_v3fl(result, proj, flat);
      }
      break;
    }
    case PART_KINK_BRAID: {
      float y_vec[3] = {0.0f, 1.0f, 0.0f};
      float z_vec[3] = {0.0f, 0.0f, 1.0f};
      float vec_one[3], state_co[3];
      float inp_y, inp_z, length;

      if (par_rot) {
        mul_qt_v3(par_rot, y_vec);
        mul_qt_v3(par_rot, z_vec);
      }

      negate_v3(par_vec);
      normalize_v3_v3(vec_one, par_vec);

      inp_y = dot_v3v3(y_vec, vec_one);
      inp_z = dot_v3v3(z_vec, vec_one);

      if (inp_y > 0.5f) {
        copy_v3_v3(state_co, y_vec);

        mul_v3_fl(y_vec, amplitude * cosf(t));
        mul_v3_fl(z_vec, amplitude / 2.0f * sinf(2.0f * t));
      }
      else if (inp_z > 0.0f) {
        mul_v3_v3fl(state_co, z_vec, sinf(float(M_PI) / 3.0f));
        madd_v3_v3fl(state_co, y_vec, -0.5f);

        mul_v3_fl(y_vec, -amplitude * cosf(t + float(M_PI) / 3.0f));
        mul_v3_fl(z_vec, amplitude / 2.0f * cosf(2.0f * t + float(M_PI) / 6.0f));
      }
      else {
        mul_v3_v3fl(state_co, z_vec, -sinf(float(M_PI) / 3.0f));
        madd_v3_v3fl(state_co, y_vec, -0.5f);

        mul_v3_fl(y_vec, amplitude * -sinf(t + float(M_PI) / 6.0f));
        mul_v3_fl(z_vec, amplitude / 2.0f * -sinf(2.0f * t + float(M_PI) / 3.0f));
      }

      mul_v3_fl(state_co, amplitude);
      add_v3_v3(state_co, par_co);
      sub_v3_v3v3(par_vec, state->co, state_co);

      length = normalize_v3(par_vec);
      mul_v3_fl(par_vec, std::min(length, amplitude / 2.0f));

      add_v3_v3v3(state_co, par_co, y_vec);
      add_v3_v3(state_co, z_vec);
      add_v3_v3(state_co, par_vec);

      shape = 2.0f * float(M_PI) * (1.0f + shape);

      if (t < shape) {
        shape = t / shape;
        shape = float(sqrt(double(shape)));
        interp_v3_v3v3(result, result, state_co, shape);
      }
      else {
        copy_v3_v3(result, state_co);
      }
      break;
    }
  }

  /* blend the start of the kink */
  if (dt < 1.0f) {
    interp_v3_v3v3(state->co, state->co, result, dt);
  }
  else {
    copy_v3_v3(state->co, result);
  }
}

static float do_clump_level(float result[3],
                            const float co[3],
                            const float par_co[3],
                            float time,
                            float clumpfac,
                            float clumppow,
                            float pa_clump,
                            const CurveMapping *clumpcurve)
{
  float clump = 0.0f;

  if (clumpcurve) {
    clump = pa_clump *
            (1.0f - clamp_f(BKE_curvemapping_evaluateF(clumpcurve, 0, time), 0.0f, 1.0f));

    interp_v3_v3v3(result, co, par_co, clump);
  }
  else if (clumpfac != 0.0f) {
    float cpow;

    if (clumppow < 0.0f) {
      cpow = 1.0f + clumppow;
    }
    else {
      cpow = 1.0f + 9.0f * clumppow;
    }

    if (clumpfac < 0.0f) { /* clump roots instead of tips */
      clump = -clumpfac * pa_clump * float(pow(1.0 - double(time), double(cpow)));
    }
    else {
      clump = clumpfac * pa_clump * float(pow(double(time), double(cpow)));
    }

    interp_v3_v3v3(result, co, par_co, clump);
  }

  return clump;
}

float do_clump(ParticleKey *state,
               const float par_co[3],
               float time,
               const float orco_offset[3],
               float clumpfac,
               float clumppow,
               float pa_clump,
               bool use_clump_noise,
               float clump_noise_size,
               const CurveMapping *clumpcurve)
{
  float clump;

  if (use_clump_noise && clump_noise_size != 0.0f) {
    float center[3], noisevec[3];
    float da[4], pa[12];

    mul_v3_v3fl(noisevec, orco_offset, 1.0f / clump_noise_size);
    BLI_noise_voronoi(noisevec[0], noisevec[1], noisevec[2], da, pa, 1.0f, 0);
    mul_v3_fl(&pa[0], clump_noise_size);
    add_v3_v3v3(center, par_co, &pa[0]);

    do_clump_level(state->co, state->co, center, time, clumpfac, clumppow, pa_clump, clumpcurve);
  }

  clump = do_clump_level(
      state->co, state->co, par_co, time, clumpfac, clumppow, pa_clump, clumpcurve);

  return clump;
}

static void do_rough(const float loc[3],
                     const float mat[4][4],
                     float t,
                     float fac,
                     float size,
                     float thres,
                     ParticleKey *state)
{
  float rough[3];
  float rco[3];

  if (thres != 0.0f) {
    if (fabsf(float(-1.5f + loc[0] + loc[1] + loc[2])) < 1.5f * thres) {
      return;
    }
  }

  copy_v3_v3(rco, loc);
  mul_v3_fl(rco, t);
  rough[0] = -1.0f +
             2.0f * BLI_noise_generic_turbulence(size, rco[0], rco[1], rco[2], 2, false, 2);
  rough[1] = -1.0f +
             2.0f * BLI_noise_generic_turbulence(size, rco[1], rco[2], rco[0], 2, false, 2);
  rough[2] = -1.0f +
             2.0f * BLI_noise_generic_turbulence(size, rco[2], rco[0], rco[1], 2, false, 2);

  madd_v3_v3fl(state->co, mat[0], fac * rough[0]);
  madd_v3_v3fl(state->co, mat[1], fac * rough[1]);
  madd_v3_v3fl(state->co, mat[2], fac * rough[2]);
}

static void do_rough_end(
    const float loc[3], const float mat[4][4], float t, float fac, float shape, ParticleKey *state)
{
  float rough[2];
  float roughfac;

  roughfac = fac * float(pow(double(t), shape));
  copy_v2_v2(rough, loc);
  rough[0] = -1.0f + 2.0f * rough[0];
  rough[1] = -1.0f + 2.0f * rough[1];
  mul_v2_fl(rough, roughfac);

  madd_v3_v3fl(state->co, mat[0], rough[0]);
  madd_v3_v3fl(state->co, mat[1], rough[1]);
}

static void do_rough_curve(const float loc[3],
                           const float mat[4][4],
                           float time,
                           float fac,
                           float size,
                           CurveMapping *roughcurve,
                           ParticleKey *state)
{
  float rough[3];
  float rco[3];

  if (!roughcurve) {
    return;
  }

  fac *= clamp_f(BKE_curvemapping_evaluateF(roughcurve, 0, time), 0.0f, 1.0f);

  copy_v3_v3(rco, loc);
  mul_v3_fl(rco, time);
  rough[0] = -1.0f +
             2.0f * BLI_noise_generic_turbulence(size, rco[0], rco[1], rco[2], 2, false, 2);
  rough[1] = -1.0f +
             2.0f * BLI_noise_generic_turbulence(size, rco[1], rco[2], rco[0], 2, false, 2);
  rough[2] = -1.0f +
             2.0f * BLI_noise_generic_turbulence(size, rco[2], rco[0], rco[1], 2, false, 2);

  madd_v3_v3fl(state->co, mat[0], fac * rough[0]);
  madd_v3_v3fl(state->co, mat[1], fac * rough[1]);
  madd_v3_v3fl(state->co, mat[2], fac * rough[2]);
}

static int twist_num_segments(const ParticleChildModifierContext *modifier_ctx)
{
  ParticleThreadContext *thread_ctx = modifier_ctx->thread_ctx;
  return (thread_ctx != nullptr) ? thread_ctx->segments : modifier_ctx->sim->psys->part->draw_step;
}

static void twist_get_axis(const ParticleChildModifierContext *modifier_ctx,
                           const float time,
                           float r_axis[3])
{
  const int num_segments = twist_num_segments(modifier_ctx);
  const int index = clamp_i(time * num_segments, 0, num_segments);
  if (index > 0) {
    sub_v3_v3v3(
        r_axis, modifier_ctx->parent_keys[index].co, modifier_ctx->parent_keys[index - 1].co);
  }
  else {
    sub_v3_v3v3(
        r_axis, modifier_ctx->parent_keys[index + 1].co, modifier_ctx->parent_keys[index].co);
  }
}

static float BKE_curvemapping_integrate_clamped(const CurveMapping *curve,
                                                float start,
                                                float end,
                                                float step)
{
  float integral = 0.0f;
  float x = start;
  while (x < end) {
    float y = BKE_curvemapping_evaluateF(curve, 0, x);
    y = clamp_f(y, 0.0f, 1.0f);
    /* TODO(sergey): Clamp last step to end. */
    integral += y * step;
    x += step;
  }
  return integral;
}

static void do_twist(const ParticleChildModifierContext *modifier_ctx,
                     ParticleKey *state,
                     const float time)
{
  ParticleThreadContext *thread_ctx = modifier_ctx->thread_ctx;
  ParticleSimulationData *sim = modifier_ctx->sim;
  ParticleTexture *ptex = modifier_ctx->ptex;
  ParticleSettings *part = sim->psys->part;
  /* Early output checks. */
  if (modifier_ctx->parent_keys == nullptr) {
    /* Cannot get axis of rotation... */
    return;
  }
  if (part->childtype != PART_CHILD_PARTICLES) {
    /* Interpolated children behave weird with twist. */
    return;
  }
  if (part->twist == 0.0f) {
    /* No twist along the strand. */
    return;
  }
  /* Dependent on whether it's threaded update or not, curve comes
   * from different places.
   */
  CurveMapping *twist_curve = nullptr;
  if (part->child_flag & PART_CHILD_USE_TWIST_CURVE) {
    twist_curve = (thread_ctx != nullptr) ? thread_ctx->twistcurve : part->twistcurve;
  }
  /* Axis of rotation. */
  float axis[3];
  twist_get_axis(modifier_ctx, time, axis);
  /* Angle of rotation. */
  float angle = part->twist;
  if (ptex != nullptr) {
    angle *= (ptex->twist - 0.5f) * 2.0f;
  }
  if (twist_curve != nullptr) {
    const int num_segments = twist_num_segments(modifier_ctx);
    angle *= BKE_curvemapping_integrate_clamped(twist_curve, 0.0f, time, 1.0f / num_segments);
  }
  else {
    angle *= time;
  }
  /* Perform rotation around parent curve. */
  float vec[3];
  sub_v3_v3v3(vec, state->co, modifier_ctx->par_co);
  rotate_v3_v3v3fl(state->co, vec, axis, angle * 2.0f * M_PI);
  add_v3_v3(state->co, modifier_ctx->par_co);
}

void do_child_modifiers(const ParticleChildModifierContext *modifier_ctx,
                        float mat[4][4],
                        ParticleKey *state,
                        float t)
{
  ParticleThreadContext *ctx = modifier_ctx->thread_ctx;
  ParticleSimulationData *sim = modifier_ctx->sim;
  ParticleTexture *ptex = modifier_ctx->ptex;
  ChildParticle *cpa = modifier_ctx->cpa;
  ParticleSettings *part = sim->psys->part;
  CurveMapping *clumpcurve = nullptr, *roughcurve = nullptr;
  int i = cpa - sim->psys->child;
  int guided = 0;

  if (part->child_flag & PART_CHILD_USE_CLUMP_CURVE) {
    clumpcurve = (ctx != nullptr) ? ctx->clumpcurve : part->clumpcurve;
  }
  if (part->child_flag & PART_CHILD_USE_ROUGH_CURVE) {
    roughcurve = (ctx != nullptr) ? ctx->roughcurve : part->roughcurve;
  }

  float kink_amp = part->kink_amp;
  float kink_amp_clump = part->kink_amp_clump;
  float kink_freq = part->kink_freq;
  float rough1 = part->rough1;
  float rough2 = part->rough2;
  float rough_end = part->rough_end;
  const bool smooth_start = (sim->psys->part->childtype == PART_CHILD_FACES);

  if (ptex) {
    kink_amp *= ptex->kink_amp;
    kink_freq *= ptex->kink_freq;
    rough1 *= ptex->rough1;
    rough2 *= ptex->rough2;
    rough_end *= ptex->roughe;
  }

  do_twist(modifier_ctx, state, t);

  if (part->flag & PART_CHILD_EFFECT) {
    /* state is safe to cast, since only co and vel are used */
    guided = do_guides(
        sim->depsgraph, sim->psys->part, sim->psys->effectors, state, cpa->parent, t);
  }

  if (guided == 0) {
    float orco_offset[3];
    float clump;

    sub_v3_v3v3(orco_offset, modifier_ctx->orco, modifier_ctx->par_orco);
    clump = do_clump(state,
                     modifier_ctx->par_co,
                     t,
                     orco_offset,
                     part->clumpfac,
                     part->clumppow,
                     ptex ? ptex->clump : 1.0f,
                     part->child_flag & PART_CHILD_USE_CLUMP_NOISE,
                     part->clump_noise_size,
                     clumpcurve);

    if (kink_freq != 0.0f) {
      kink_amp *= (1.0f - kink_amp_clump * clump);

      do_kink(state,
              modifier_ctx->par_co,
              modifier_ctx->par_vel,
              modifier_ctx->par_rot,
              t,
              kink_freq,
              part->kink_shape,
              kink_amp,
              part->kink_flat,
              part->kink,
              part->kink_axis,
              sim->ob->object_to_world().ptr(),
              smooth_start);
    }
  }

  if (roughcurve) {
    do_rough_curve(modifier_ctx->orco, mat, t, rough1, part->rough1_size, roughcurve, state);
  }
  else {
    if (rough1 > 0.0f) {
      do_rough(modifier_ctx->orco, mat, t, rough1, part->rough1_size, 0.0, state);
    }

    if (rough2 > 0.0f) {
      float vec[3];
      psys_frand_vec(sim->psys, i + 27, vec);
      do_rough(vec, mat, t, rough2, part->rough2_size, part->rough2_thres, state);
    }

    if (rough_end > 0.0f) {
      float vec[3];
      psys_frand_vec(sim->psys, i + 27, vec);
      do_rough_end(vec, mat, t, rough_end, part->rough_end_shape, state);
    }
  }
}

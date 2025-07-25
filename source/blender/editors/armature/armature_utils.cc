/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup edarmature
 */

#include "DNA_armature_types.h"
#include "DNA_object_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"
#include "BLI_math_matrix.h"
#include "BLI_math_vector.h"
#include "BLI_string_utf8.h"
#include "BLI_string_utils.hh"

#include "BKE_armature.hh"
#include "BKE_global.hh"
#include "BKE_idprop.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"

#include "DEG_depsgraph.hh"

#include "ED_armature.hh"

#include "ANIM_armature.hh"
#include "ANIM_bone_collections.hh"

#include "armature_intern.hh"

#include <cstring>

/* -------------------------------------------------------------------- */
/** \name Validation
 * \{ */

void ED_armature_edit_sync_selection(ListBase *edbo)
{
  LISTBASE_FOREACH (EditBone *, ebo, edbo) {
    /* if bone is not selectable, we shouldn't alter this setting... */
    if ((ebo->flag & BONE_UNSELECTABLE) == 0) {
      if ((ebo->flag & BONE_CONNECTED) && (ebo->parent)) {
        if (ebo->parent->flag & BONE_TIPSEL) {
          ebo->flag |= BONE_ROOTSEL;
        }
        else {
          ebo->flag &= ~BONE_ROOTSEL;
        }
      }

      if ((ebo->flag & BONE_TIPSEL) && (ebo->flag & BONE_ROOTSEL)) {
        ebo->flag |= BONE_SELECTED;
      }
      else {
        ebo->flag &= ~BONE_SELECTED;
      }
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Bone Operations
 * \{ */

int bone_looper(Object *ob, Bone *bone, void *data, int (*bone_func)(Object *, Bone *, void *))
{
  /* We want to apply the function bone_func to every bone
   * in an armature -- feed bone_looper the first bone and
   * a pointer to the bone_func and watch it go! The int count
   * can be useful for counting bones with a certain property
   * (e.g. skinnable)
   */
  int count = 0;

  if (bone) {
    /* only do bone_func if the bone is non null */
    count += bone_func(ob, bone, data);

    /* try to execute bone_func for the first child */
    count += bone_looper(ob, static_cast<Bone *>(bone->childbase.first), data, bone_func);

    /* try to execute bone_func for the next bone at this
     * depth of the recursion.
     */
    count += bone_looper(ob, bone->next, data, bone_func);
  }

  return count;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Bone Removal
 * \{ */

void bone_free(bArmature *arm, EditBone *bone)
{
  if (arm->act_edbone == bone) {
    arm->act_edbone = nullptr;
  }

  if (bone->prop) {
    IDP_FreeProperty(bone->prop);
  }
  if (bone->system_properties) {
    IDP_FreeProperty(bone->system_properties);
  }

  /* Clear references from other edit bones. */
  LISTBASE_FOREACH (EditBone *, ebone, arm->edbo) {
    if (ebone->bbone_next == bone) {
      ebone->bbone_next = nullptr;
    }
    if (ebone->bbone_prev == bone) {
      ebone->bbone_prev = nullptr;
    }
  }

  BLI_freelistN(&bone->bone_collections);
  BLI_freelinkN(arm->edbo, bone);
}

void ED_armature_ebone_remove_ex(bArmature *arm, EditBone *exBone, bool clear_connected)
{
  /* Find any bones that refer to this bone */
  LISTBASE_FOREACH (EditBone *, curBone, arm->edbo) {
    if (curBone->parent == exBone) {
      curBone->parent = exBone->parent;
      if (clear_connected) {
        curBone->flag &= ~BONE_CONNECTED;
      }
    }
  }

  bone_free(arm, exBone);
}

void ED_armature_ebone_remove(bArmature *arm, EditBone *exBone)
{
  ED_armature_ebone_remove_ex(arm, exBone, true);
}

bool ED_armature_ebone_is_child_recursive(EditBone *ebone_parent, EditBone *ebone_child)
{
  for (ebone_child = ebone_child->parent; ebone_child; ebone_child = ebone_child->parent) {
    if (ebone_child == ebone_parent) {
      return true;
    }
  }
  return false;
}

EditBone *ED_armature_ebone_find_shared_parent(EditBone *ebone_child[], const uint ebone_child_tot)
{
#define EBONE_TEMP_UINT(ebone) (*((uint *)(&((ebone)->temp))))

  /* clear all */
  for (uint i = 0; i < ebone_child_tot; i++) {
    for (EditBone *ebone_iter = ebone_child[i]; ebone_iter; ebone_iter = ebone_iter->parent) {
      EBONE_TEMP_UINT(ebone_iter) = 0;
    }
  }

  /* accumulate */
  for (uint i = 0; i < ebone_child_tot; i++) {
    for (EditBone *ebone_iter = ebone_child[i]->parent; ebone_iter;
         ebone_iter = ebone_iter->parent)
    {
      EBONE_TEMP_UINT(ebone_iter) += 1;
    }
  }

  /* only need search the first chain */
  for (EditBone *ebone_iter = ebone_child[0]->parent; ebone_iter; ebone_iter = ebone_iter->parent)
  {
    if (EBONE_TEMP_UINT(ebone_iter) == ebone_child_tot) {
      return ebone_iter;
    }
  }

#undef EBONE_TEMP_UINT

  return nullptr;
}

void ED_armature_ebone_to_mat3(EditBone *ebone, float r_mat[3][3])
{
  float delta[3], roll;

  /* Find the current bone matrix */
  sub_v3_v3v3(delta, ebone->tail, ebone->head);
  roll = ebone->roll;
  if (!normalize_v3(delta)) {
    /* Use the orientation of the parent bone if any. */
    const EditBone *ebone_parent = ebone->parent;
    if (ebone_parent) {
      sub_v3_v3v3(delta, ebone_parent->tail, ebone_parent->head);
      normalize_v3(delta);
      roll = ebone_parent->roll;
    }
  }

  vec_roll_to_mat3_normalized(delta, roll, r_mat);
}

void ED_armature_ebone_to_mat4(EditBone *ebone, float r_mat[4][4])
{
  float m3[3][3];

  ED_armature_ebone_to_mat3(ebone, m3);

  copy_m4_m3(r_mat, m3);
  copy_v3_v3(r_mat[3], ebone->head);
}

void ED_armature_ebone_from_mat3(EditBone *ebone, const float mat[3][3])
{
  float vec[3], roll;
  const float len = len_v3v3(ebone->head, ebone->tail);

  mat3_to_vec_roll(mat, vec, &roll);

  madd_v3_v3v3fl(ebone->tail, ebone->head, vec, len);
  ebone->roll = roll;
}

void ED_armature_ebone_from_mat4(EditBone *ebone, const float mat[4][4])
{
  float mat3[3][3];

  copy_m3_m4(mat3, mat);
  /* We want normalized matrix here, to be consistent with ebone_to_mat. */
  BLI_ASSERT_UNIT_M3(mat3);

  sub_v3_v3(ebone->tail, ebone->head);
  copy_v3_v3(ebone->head, mat[3]);
  add_v3_v3(ebone->tail, mat[3]);
  ED_armature_ebone_from_mat3(ebone, mat3);
}

EditBone *ED_armature_ebone_find_name(const ListBase *edbo, const char *name)
{
  return static_cast<EditBone *>(BLI_findstring(edbo, name, offsetof(EditBone, name)));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Mirroring
 * \{ */

EditBone *ED_armature_ebone_get_mirrored(const ListBase *edbo, EditBone *ebo)
{
  char name_flip[MAXBONENAME];

  if (ebo == nullptr) {
    return nullptr;
  }

  BLI_string_flip_side_name(name_flip, ebo->name, false, sizeof(name_flip));

  if (!STREQ(name_flip, ebo->name)) {
    return ED_armature_ebone_find_name(edbo, name_flip);
  }

  return nullptr;
}

/* ------------------------------------- */

void armature_select_mirrored_ex(bArmature *arm, const int flag)
{
  BLI_assert((flag & ~(BONE_SELECTED | BONE_ROOTSEL | BONE_TIPSEL)) == 0);
  /* Select mirrored bones */
  if (arm->flag & ARM_MIRROR_EDIT) {
    LISTBASE_FOREACH (EditBone *, curBone, arm->edbo) {
      if (blender::animrig::bone_is_visible_editbone(arm, curBone)) {
        if (curBone->flag & flag) {
          EditBone *ebone_mirr = ED_armature_ebone_get_mirrored(arm->edbo, curBone);
          if (ebone_mirr) {
            ebone_mirr->flag |= (curBone->flag & flag);
          }
        }
      }
    }
  }
}

void armature_select_mirrored(bArmature *arm)
{
  armature_select_mirrored_ex(arm, BONE_SELECTED);
}

void armature_tag_select_mirrored(bArmature *arm)
{
  /* always untag */
  LISTBASE_FOREACH (EditBone *, curBone, arm->edbo) {
    curBone->flag &= ~BONE_DONE;
  }

  /* Select mirrored bones */
  if (arm->flag & ARM_MIRROR_EDIT) {
    LISTBASE_FOREACH (EditBone *, curBone, arm->edbo) {
      if (blender::animrig::bone_is_visible_editbone(arm, curBone)) {
        if (curBone->flag & (BONE_SELECTED | BONE_ROOTSEL | BONE_TIPSEL)) {
          EditBone *ebone_mirr = ED_armature_ebone_get_mirrored(arm->edbo, curBone);
          if (ebone_mirr && (ebone_mirr->flag & BONE_SELECTED) == 0) {
            ebone_mirr->flag |= BONE_DONE;
          }
        }
      }
    }

    LISTBASE_FOREACH (EditBone *, curBone, arm->edbo) {
      if (curBone->flag & BONE_DONE) {
        EditBone *ebone_mirr = ED_armature_ebone_get_mirrored(arm->edbo, curBone);
        curBone->flag |= ebone_mirr->flag & (BONE_SELECTED | BONE_ROOTSEL | BONE_TIPSEL);
      }
    }
  }
}

void armature_tag_unselect(bArmature *arm)
{
  LISTBASE_FOREACH (EditBone *, curBone, arm->edbo) {
    if (curBone->flag & BONE_DONE) {
      curBone->flag &= ~(BONE_SELECTED | BONE_ROOTSEL | BONE_TIPSEL | BONE_DONE);
    }
  }
}

/* ------------------------------------- */

void ED_armature_ebone_transform_mirror_update(bArmature *arm, EditBone *ebo, bool check_select)
{
  /* TODO: When this function is called by property updates,
   * canceling the value change will not restore mirrored bone correctly. */

  /* Currently check_select==true when this function is called from a transform operator,
   * eg. from 3d viewport. */

  /* no layer check, correct mirror is more important */
  if (!check_select || (blender::animrig::bone_is_visible_editbone(arm, ebo) &&
                        (ebo->flag & (BONE_TIPSEL | BONE_ROOTSEL))))
  {
    EditBone *eboflip = ED_armature_ebone_get_mirrored(arm->edbo, ebo);
    if (eboflip) {
      /* We assume X-axis flipping for now. */

      /* Always mirror roll, since it can be changed by moving either head or tail. */
      eboflip->roll = -ebo->roll;

      if (!check_select || ebo->flag & BONE_TIPSEL) {
        /* Mirror tail properties. */

        eboflip->tail[0] = -ebo->tail[0];
        eboflip->tail[1] = ebo->tail[1];
        eboflip->tail[2] = ebo->tail[2];
        eboflip->rad_tail = ebo->rad_tail;
        eboflip->curve_out_x = -ebo->curve_out_x;
        eboflip->curve_out_z = ebo->curve_out_z;
        copy_v3_v3(eboflip->scale_out, ebo->scale_out);
        eboflip->ease2 = ebo->ease2;
        eboflip->roll2 = -ebo->roll2;

        /* Also move connected children, in case children's name aren't mirrored properly. */
        LISTBASE_FOREACH (EditBone *, children, arm->edbo) {
          if (children->parent == eboflip && children->flag & BONE_CONNECTED) {
            copy_v3_v3(children->head, eboflip->tail);
            children->rad_head = ebo->rad_tail;
          }
        }
      }

      if (!check_select || ebo->flag & BONE_ROOTSEL) {
        /* Mirror head properties. */
        eboflip->head[0] = -ebo->head[0];
        eboflip->head[1] = ebo->head[1];
        eboflip->head[2] = ebo->head[2];
        eboflip->rad_head = ebo->rad_head;

        eboflip->curve_in_x = -ebo->curve_in_x;
        eboflip->curve_in_z = ebo->curve_in_z;
        copy_v3_v3(eboflip->scale_in, ebo->scale_in);
        eboflip->ease1 = ebo->ease1;
        eboflip->roll1 = -ebo->roll1;

        /* Also move connected parent, in case parent's name isn't mirrored properly. */
        if (eboflip->parent && eboflip->flag & BONE_CONNECTED) {
          EditBone *parent = eboflip->parent;
          copy_v3_v3(parent->tail, eboflip->head);
          parent->rad_tail = ebo->rad_head;
        }
      }

      if (!check_select || ebo->flag & BONE_SELECTED) {
        /* Mirror bone body properties (both head and tail are selected). */
        /* TODO: These values can also be changed from pose mode,
         * so only mirroring them in edit mode is not ideal. */
        eboflip->dist = ebo->dist;
        eboflip->weight = ebo->weight;

        eboflip->segments = ebo->segments;
        eboflip->xwidth = ebo->xwidth;
        eboflip->zwidth = ebo->zwidth;
      }
    }
  }
}

void ED_armature_edit_transform_mirror_update(Object *obedit)
{
  bArmature *arm = static_cast<bArmature *>(obedit->data);
  LISTBASE_FOREACH (EditBone *, ebo, arm->edbo) {
    ED_armature_ebone_transform_mirror_update(arm, ebo, true);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Armature EditMode Conversions
 * \{ */

/**
 * Copy the bone collection membership info from the bones to the edit-bones.
 *
 * Operations on edit-bones (like subdividing, extruding, etc.) will have to deal
 * with collection assignments of those edit-bones as well.
 */
static void copy_bonecollection_membership(EditBone *eBone, const Bone *bone)
{
  BLI_assert(BLI_listbase_is_empty(&eBone->bone_collections));
  BLI_duplicatelist(&eBone->bone_collections, &bone->runtime.collections);
}

/* converts Bones to EditBone list, used for tools as well */
static EditBone *make_boneList_recursive(ListBase *edbo,
                                         ListBase *bones,
                                         EditBone *parent,
                                         Bone *actBone)
{
  EditBone *eBone;
  EditBone *eBoneAct = nullptr;
  EditBone *eBoneTest = nullptr;

  LISTBASE_FOREACH (Bone *, curBone, bones) {
    eBone = MEM_callocN<EditBone>("make_editbone");
    eBone->temp.bone = curBone;

    /* Copy relevant data from bone to eBone
     * Keep selection logic in sync with ED_armature_edit_sync_selection.
     */
    eBone->parent = parent;
    STRNCPY_UTF8(eBone->name, curBone->name);
    eBone->flag = curBone->flag;
    eBone->inherit_scale_mode = curBone->inherit_scale_mode;
    eBone->drawtype = curBone->drawtype;

    /* fix selection flags */
    if (eBone->flag & BONE_SELECTED) {
      /* if the bone is selected the copy its root selection to the parents tip */
      eBone->flag |= BONE_TIPSEL;
      if (eBone->parent && (eBone->flag & BONE_CONNECTED)) {
        eBone->parent->flag |= BONE_TIPSEL;
      }

      /* For connected bones, take care when changing the selection when we have a
       * connected parent, this flag is a copy of '(eBone->parent->flag & BONE_TIPSEL)'. */
      eBone->flag |= BONE_ROOTSEL;
    }
    else {
      /* if the bone is not selected, but connected to its parent
       * always use the parents tip selection state */
      if (eBone->parent && (eBone->flag & BONE_CONNECTED)) {
        eBone->flag &= ~BONE_ROOTSEL;
      }
    }

    copy_v3_v3(eBone->head, curBone->arm_head);
    copy_v3_v3(eBone->tail, curBone->arm_tail);
    eBone->roll = curBone->arm_roll;

    /* rest of stuff copy */
    eBone->length = curBone->length;
    eBone->dist = curBone->dist;
    eBone->weight = curBone->weight;
    eBone->xwidth = curBone->xwidth;
    eBone->zwidth = curBone->zwidth;
    eBone->rad_head = curBone->rad_head;
    eBone->rad_tail = curBone->rad_tail;
    eBone->segments = curBone->segments;
    eBone->layer = curBone->layer;

    /* Bendy-Bone parameters */
    eBone->roll1 = curBone->roll1;
    eBone->roll2 = curBone->roll2;
    eBone->curve_in_x = curBone->curve_in_x;
    eBone->curve_in_z = curBone->curve_in_z;
    eBone->curve_out_x = curBone->curve_out_x;
    eBone->curve_out_z = curBone->curve_out_z;
    eBone->ease1 = curBone->ease1;
    eBone->ease2 = curBone->ease2;

    copy_v3_v3(eBone->scale_in, curBone->scale_in);
    copy_v3_v3(eBone->scale_out, curBone->scale_out);

    eBone->bbone_prev_type = curBone->bbone_prev_type;
    eBone->bbone_next_type = curBone->bbone_next_type;

    eBone->bbone_mapping_mode = eBone_BBoneMappingMode(curBone->bbone_mapping_mode);
    eBone->bbone_flag = curBone->bbone_flag;
    eBone->bbone_prev_flag = curBone->bbone_prev_flag;
    eBone->bbone_next_flag = curBone->bbone_next_flag;

    eBone->color = curBone->color;
    copy_bonecollection_membership(eBone, curBone);

    if (curBone->prop) {
      eBone->prop = IDP_CopyProperty(curBone->prop);
    }
    if (curBone->system_properties) {
      eBone->system_properties = IDP_CopyProperty(curBone->system_properties);
    }

    BLI_addtail(edbo, eBone);

    /* Add children if necessary. */
    if (curBone->childbase.first) {
      eBoneTest = make_boneList_recursive(edbo, &curBone->childbase, eBone, actBone);
      if (eBoneTest) {
        eBoneAct = eBoneTest;
      }
    }

    if (curBone == actBone) {
      eBoneAct = eBone;
    }
  }

  return eBoneAct;
}

static EditBone *find_ebone_link(ListBase *edbo, Bone *link)
{
  if (link != nullptr) {
    LISTBASE_FOREACH (EditBone *, ebone, edbo) {
      if (ebone->temp.bone == link) {
        return ebone;
      }
    }
  }

  return nullptr;
}

EditBone *make_boneList(ListBase *edbo, ListBase *bones, Bone *actBone)
{
  BLI_assert(!edbo->first && !edbo->last);

  EditBone *active = make_boneList_recursive(edbo, bones, nullptr, actBone);

  LISTBASE_FOREACH (EditBone *, ebone, edbo) {
    Bone *bone = ebone->temp.bone;

    /* Convert custom B-Bone handle links. */
    ebone->bbone_prev = find_ebone_link(edbo, bone->bbone_prev);
    ebone->bbone_next = find_ebone_link(edbo, bone->bbone_next);
  }

  return active;
}

/**
 * This function:
 * - Sets local head/tail rest locations using parent bone's arm_mat.
 * - Calls #BKE_armature_where_is_bone() which uses parent's transform (arm_mat)
 *   to define this bone's transform.
 * - Fixes (converts) EditBone roll into Bone roll.
 * - Calls again #BKE_armature_where_is_bone(),
 *   since roll fiddling may have changed things for our bone.
 *
 * \note The order is crucial here, we can only handle child
 * if all its parents in chain have already been handled (this is ensured by recursive process).
 */
static void armature_finalize_restpose(ListBase *bonelist, ListBase *editbonelist)
{
  LISTBASE_FOREACH (Bone *, curBone, bonelist) {
    /* Set bone's local head/tail.
     * Note that it's important to use final parent's rest-pose (arm_mat) here,
     * instead of setting those values from edit-bone's matrix (see #46010). */
    if (curBone->parent) {
      float parmat_inv[4][4];

      invert_m4_m4(parmat_inv, curBone->parent->arm_mat);

      /* Get the new head and tail */
      sub_v3_v3v3(curBone->head, curBone->arm_head, curBone->parent->arm_tail);
      sub_v3_v3v3(curBone->tail, curBone->arm_tail, curBone->parent->arm_tail);

      mul_mat3_m4_v3(parmat_inv, curBone->head);
      mul_mat3_m4_v3(parmat_inv, curBone->tail);
    }
    else {
      copy_v3_v3(curBone->head, curBone->arm_head);
      copy_v3_v3(curBone->tail, curBone->arm_tail);
    }

    /* Set local matrix and arm_mat (rest-pose).
     * Do not recurse into children here, armature_finalize_restpose() is already recursive. */
    BKE_armature_where_is_bone(curBone, curBone->parent, false);

    /* Find the associated editbone */
    LISTBASE_FOREACH (EditBone *, ebone, editbonelist) {
      if (ebone->temp.bone == curBone) {
        float premat[3][3];
        float postmat[3][3];
        float difmat[3][3];
        float imat[3][3];

        /* Get the ebone premat and its inverse. */
        ED_armature_ebone_to_mat3(ebone, premat);
        invert_m3_m3(imat, premat);

        /* Get the bone postmat. */
        copy_m3_m4(postmat, curBone->arm_mat);

        mul_m3_m3m3(difmat, imat, postmat);

#if 0
        printf("Bone %s\n", curBone->name);
        print_m4("premat", premat);
        print_m4("postmat", postmat);
        print_m4("difmat", difmat);
        printf("Roll = %f\n", RAD2DEGF(-atan2(difmat[2][0], difmat[2][2])));
#endif

        curBone->roll = -atan2f(difmat[2][0], difmat[2][2]);

        /* And set rest-position again. */
        BKE_armature_where_is_bone(curBone, curBone->parent, false);
        break;
      }
    }

    /* Recurse into children... */
    armature_finalize_restpose(&curBone->childbase, editbonelist);
  }
}

void ED_armature_from_edit(Main *bmain, bArmature *arm)
{
  EditBone *eBone, *neBone;
  Bone *newBone;
  Object *obt;

  /* armature bones */
  BKE_armature_bone_hash_free(arm);
  BKE_armature_bonelist_free(&arm->bonebase, true);
  arm->act_bone = nullptr;

  /* Remove zero sized bones, this gives unstable rest-poses. */
  constexpr float bone_length_threshold = 0.000001f * 0.000001f;
  for (eBone = static_cast<EditBone *>(arm->edbo->first); eBone; eBone = neBone) {
    float len_sq = len_squared_v3v3(eBone->head, eBone->tail);
    neBone = eBone->next;
    if (len_sq <= bone_length_threshold) { /* FLT_EPSILON is too large? */
      /* Find any bones that refer to this bone */
      LISTBASE_FOREACH (EditBone *, fBone, arm->edbo) {
        if (fBone->parent == eBone) {
          fBone->parent = eBone->parent;
        }
      }
      if (G.debug & G_DEBUG) {
        printf("Warning: removed zero sized bone: %s\n", eBone->name);
      }
      bone_free(arm, eBone);
    }
  }

  /* Copy the bones from the edit-data into the armature. */
  LISTBASE_FOREACH (EditBone *, eBone, arm->edbo) {
    newBone = MEM_callocN<Bone>("bone");
    eBone->temp.bone = newBone; /* Associate the real Bones with the EditBones */

    STRNCPY_UTF8(newBone->name, eBone->name);
    copy_v3_v3(newBone->arm_head, eBone->head);
    copy_v3_v3(newBone->arm_tail, eBone->tail);
    newBone->arm_roll = eBone->roll;

    newBone->flag = eBone->flag;
    newBone->inherit_scale_mode = eBone->inherit_scale_mode;
    newBone->drawtype = eBone->drawtype;

    if (eBone == arm->act_edbone) {
      /* Don't change active selection, this messes up separate which uses
       * edit-mode toggle and can separate active bone which is de-selected originally. */

      /* important, edit-bones can be active with only 1 point selected */
      /* `newBone->flag |= BONE_SELECTED;` */
      arm->act_bone = newBone;
    }
    newBone->roll = 0.0f;

    newBone->weight = eBone->weight;
    newBone->dist = eBone->dist;

    newBone->xwidth = eBone->xwidth;
    newBone->zwidth = eBone->zwidth;
    newBone->rad_head = eBone->rad_head;
    newBone->rad_tail = eBone->rad_tail;
    newBone->segments = eBone->segments;
    newBone->layer = eBone->layer;

    /* Bendy-Bone parameters */
    newBone->roll1 = eBone->roll1;
    newBone->roll2 = eBone->roll2;
    newBone->curve_in_x = eBone->curve_in_x;
    newBone->curve_in_z = eBone->curve_in_z;
    newBone->curve_out_x = eBone->curve_out_x;
    newBone->curve_out_z = eBone->curve_out_z;
    newBone->ease1 = eBone->ease1;
    newBone->ease2 = eBone->ease2;
    copy_v3_v3(newBone->scale_in, eBone->scale_in);
    copy_v3_v3(newBone->scale_out, eBone->scale_out);

    newBone->bbone_prev_type = eBone->bbone_prev_type;
    newBone->bbone_next_type = eBone->bbone_next_type;

    newBone->bbone_mapping_mode = eBone->bbone_mapping_mode;
    newBone->bbone_flag = eBone->bbone_flag;
    newBone->bbone_prev_flag = eBone->bbone_prev_flag;
    newBone->bbone_next_flag = eBone->bbone_next_flag;

    newBone->color = eBone->color;

    LISTBASE_FOREACH (BoneCollectionReference *, ref, &eBone->bone_collections) {
      BoneCollectionReference *newBoneRef = MEM_dupallocN<BoneCollectionReference>(
          "ED_armature_from_edit", *ref);
      BLI_addtail(&newBone->runtime.collections, newBoneRef);
    }

    if (eBone->prop) {
      newBone->prop = IDP_CopyProperty(eBone->prop);
    }
    if (eBone->system_properties) {
      newBone->system_properties = IDP_CopyProperty(eBone->system_properties);
    }
  }

  /* Fix parenting in a separate pass to ensure ebone->bone connections are valid at this point.
   * Do not set bone->head/tail here anymore,
   * using EditBone data for that is not OK since our later fiddling with parent's arm_mat
   * (for roll conversion) may have some small but visible impact on locations (#46010). */
  LISTBASE_FOREACH (EditBone *, eBone, arm->edbo) {
    newBone = eBone->temp.bone;
    if (eBone->parent) {
      newBone->parent = eBone->parent->temp.bone;
      BLI_addtail(&newBone->parent->childbase, newBone);
    }
    /*  ...otherwise add this bone to the armature's bonebase */
    else {
      BLI_addtail(&arm->bonebase, newBone);
    }

    /* Also transfer B-Bone custom handles. */
    if (eBone->bbone_prev) {
      newBone->bbone_prev = eBone->bbone_prev->temp.bone;
    }
    if (eBone->bbone_next) {
      newBone->bbone_next = eBone->bbone_next->temp.bone;
    }
  }

  /* Finalize definition of rest-pose data (roll, bone_mat, arm_mat, head/tail...). */
  armature_finalize_restpose(&arm->bonebase, arm->edbo);
  ANIM_armature_bonecoll_reconstruct(arm);

  BKE_armature_bone_hash_make(arm);

  /* so all users of this armature should get rebuilt */
  for (obt = static_cast<Object *>(bmain->objects.first); obt;
       obt = static_cast<Object *>(obt->id.next))
  {
    if (obt->data == arm) {
      BKE_pose_rebuild(bmain, obt, arm, true);
    }
  }

  DEG_id_tag_update(&arm->id, 0);
}

void ED_armature_edit_free(bArmature *arm)
{
  /* Clear the edit-bones list. */
  if (arm->edbo) {
    if (arm->edbo->first) {
      LISTBASE_FOREACH (EditBone *, eBone, arm->edbo) {
        if (eBone->prop) {
          IDP_FreeProperty(eBone->prop);
        }
        if (eBone->system_properties) {
          IDP_FreeProperty(eBone->system_properties);
        }
        BLI_freelistN(&eBone->bone_collections);
      }

      BLI_freelistN(arm->edbo);
    }
    MEM_freeN(arm->edbo);
    arm->edbo = nullptr;
    arm->act_edbone = nullptr;
  }
}

void ED_armature_to_edit(bArmature *arm)
{
  ED_armature_edit_free(arm);
  arm->edbo = MEM_callocN<ListBase>("edbo armature");
  arm->act_edbone = make_boneList(arm->edbo, &arm->bonebase, arm->act_bone);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Used by Undo for Armature EditMode
 * \{ */

void ED_armature_ebone_listbase_free(ListBase *lb, const bool do_id_user)
{
  EditBone *ebone, *ebone_next;

  for (ebone = static_cast<EditBone *>(lb->first); ebone; ebone = ebone_next) {
    ebone_next = ebone->next;

    if (ebone->prop) {
      IDP_FreeProperty_ex(ebone->prop, do_id_user);
    }
    if (ebone->system_properties) {
      IDP_FreeProperty_ex(ebone->system_properties, do_id_user);
    }

    BLI_freelistN(&ebone->bone_collections);

    MEM_freeN(ebone);
  }

  BLI_listbase_clear(lb);
}

void ED_armature_ebone_listbase_copy(ListBase *lb_dst, ListBase *lb_src, const bool do_id_user)
{
  BLI_assert(BLI_listbase_is_empty(lb_dst));

  LISTBASE_FOREACH (EditBone *, ebone_src, lb_src) {
    EditBone *ebone_dst = static_cast<EditBone *>(MEM_dupallocN(ebone_src));
    if (ebone_dst->prop) {
      ebone_dst->prop = IDP_CopyProperty_ex(ebone_dst->prop,
                                            do_id_user ? 0 : LIB_ID_CREATE_NO_USER_REFCOUNT);
    }
    if (ebone_dst->system_properties) {
      ebone_dst->system_properties = IDP_CopyProperty_ex(
          ebone_dst->system_properties, do_id_user ? 0 : LIB_ID_CREATE_NO_USER_REFCOUNT);
    }
    ebone_src->temp.ebone = ebone_dst;
    BLI_addtail(lb_dst, ebone_dst);
  }

  /* set pointers */
  LISTBASE_FOREACH (EditBone *, ebone_dst, lb_dst) {
    if (ebone_dst->parent) {
      ebone_dst->parent = ebone_dst->parent->temp.ebone;
    }
    if (ebone_dst->bbone_next) {
      ebone_dst->bbone_next = ebone_dst->bbone_next->temp.ebone;
    }
    if (ebone_dst->bbone_prev) {
      ebone_dst->bbone_prev = ebone_dst->bbone_prev->temp.ebone;
    }

    BLI_duplicatelist(&ebone_dst->bone_collections, &ebone_dst->bone_collections);
  }
}

void ED_armature_ebone_listbase_temp_clear(ListBase *lb)
{
  /* be sure they don't hang ever */
  LISTBASE_FOREACH (EditBone *, ebone, lb) {
    ebone->temp.p = nullptr;
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Low Level Selection Functions
 *
 * which hide connected-parent flag behavior which gets tricky to handle in selection operators.
 * (no flushing in `ED_armature_ebone_select.*`, that should be explicit).
 * \{ */

int ED_armature_ebone_selectflag_get(const EditBone *ebone)
{
  if (ebone->parent && (ebone->flag & BONE_CONNECTED)) {
    return ((ebone->flag & (BONE_SELECTED | BONE_TIPSEL)) |
            ((ebone->parent->flag & BONE_TIPSEL) ? BONE_ROOTSEL : 0));
  }
  return (ebone->flag & (BONE_SELECTED | BONE_ROOTSEL | BONE_TIPSEL));
}

void ED_armature_ebone_selectflag_set(EditBone *ebone, int flag)
{
  flag = flag & (BONE_SELECTED | BONE_ROOTSEL | BONE_TIPSEL);

  if (ebone->parent && (ebone->flag & BONE_CONNECTED)) {
    ebone->flag &= ~(BONE_SELECTED | BONE_ROOTSEL | BONE_TIPSEL);
    ebone->parent->flag &= ~BONE_TIPSEL;

    ebone->flag |= flag;
    ebone->parent->flag |= (flag & BONE_ROOTSEL) ? BONE_TIPSEL : 0;
  }
  else {
    ebone->flag &= ~(BONE_SELECTED | BONE_ROOTSEL | BONE_TIPSEL);
    ebone->flag |= flag;
  }
}

void ED_armature_ebone_selectflag_enable(EditBone *ebone, int flag)
{
  BLI_assert((flag & (BONE_SELECTED | BONE_ROOTSEL | BONE_TIPSEL)) != 0);
  ED_armature_ebone_selectflag_set(ebone, ebone->flag | flag);
}

void ED_armature_ebone_selectflag_disable(EditBone *ebone, int flag)
{
  BLI_assert((flag & (BONE_SELECTED | BONE_ROOTSEL | BONE_TIPSEL)) != 0);
  ED_armature_ebone_selectflag_set(ebone, ebone->flag & ~flag);
}

void ED_armature_ebone_select_set(EditBone *ebone, bool select)
{
  /* NOTE: this function could be used in more places. */

  int flag;
  if (select) {
    BLI_assert((ebone->flag & BONE_UNSELECTABLE) == 0);
    flag = (BONE_SELECTED | BONE_TIPSEL | BONE_ROOTSEL);
  }
  else {
    flag = 0;
  }
  ED_armature_ebone_selectflag_set(ebone, flag);
}

/** \} */

/* SPDX-FileCopyrightText: 2007 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 */

#pragma once

#include <string>

struct bNodeTree;

/** Groups display their internal tree name as label. */
void node_group_label(const struct bNodeTree *ntree,
                      const struct bNode *node,
                      char *label,
                      int label_maxncpy);
bool node_group_poll_instance(const struct bNode *node,
                              const struct bNodeTree *nodetree,
                              const char **r_disabled_hint);
int node_group_ui_class(const struct bNode *node);

/**
 * Global update function for Reroute node types.
 * This depends on connected nodes, so must be done as a tree-wide update.
 */
void ntree_update_reroute_nodes(struct bNodeTree *ntree);

std::string node_group_ui_description(const bNode &node);

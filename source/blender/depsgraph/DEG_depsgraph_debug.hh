/* SPDX-FileCopyrightText: 2014 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup depsgraph
 *
 * Public API for Querying and Filtering Depsgraph
 */

#pragma once

#include <cstdio>
#include <string>

#include "BLI_string_ref.hh"

struct Depsgraph;
struct Main;
struct Scene;
struct ViewLayer;

/* ------------------------------------------------ */

/* NOTE: Those flags are same bit-mask as #G.debug_flags */

void DEG_debug_flags_set(Depsgraph *depsgraph, int flags);
int DEG_debug_flags_get(const Depsgraph *depsgraph);

void DEG_debug_name_set(Depsgraph *depsgraph, const char *name);
const char *DEG_debug_name_get(Depsgraph *depsgraph);

/* ------------------------------------------------ */

/**
 * Obtain simple statistics about the complexity of the depsgraph.
 * \param[out] r_outer:      The number of outer nodes in the graph.
 * \param[out] r_operations: The number of operation nodes in the graph.
 * \param[out] r_relations:  The number of relations between (executable) nodes in the graph.
 */
void DEG_stats_simple(const Depsgraph *graph,
                      size_t *r_outer,
                      size_t *r_operations,
                      size_t *r_relations);

/* ************************************************ */
/* Diagram-Based Graph Debugging */

std::string DEG_debug_graph_to_dot(const Depsgraph &graph, blender::StringRef label);

void DEG_debug_stats_gnuplot(const Depsgraph *graph,
                             FILE *fp,
                             const char *label,
                             const char *output_filename);

/* ************************************************ */

/** Compare two dependency graphs. */
bool DEG_debug_compare(const Depsgraph *graph1, const Depsgraph *graph2);

/** Check that dependencies in the graph are really up to date. */
bool DEG_debug_graph_relations_validate(Depsgraph *graph,
                                        Main *bmain,
                                        Scene *scene,
                                        ViewLayer *view_layer);

/** Perform consistency check on the graph. */
bool DEG_debug_consistency_check(Depsgraph *graph);

/**
 * Convert bit-flag representation of recalculation flags to string
 * that consists of human-readable names of recalculation bits that are set in the given mask.
 *
 * If flags == 0 the result is "NONE".
 *
 * Example:
 *   DEG_stringify_recalc_flags(ID_RECALC_GEOMETRY | ID_RECALC_SHADING) -> "GEOMETRY, SHADING".
 */
std::string DEG_stringify_recalc_flags(unsigned int flags);

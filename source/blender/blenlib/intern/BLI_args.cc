/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup bli
 * \brief A general argument parsing module
 */

#include <cctype> /* for tolower */
#include <cstdio>

#include "MEM_guardedalloc.h"

#include "BLI_args.h"
#include "BLI_ghash.h"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

/**
 * Needed so printing `--help` doesn't cause a naming collision with:
 * The `-a` argument which is used twice.
 * This works for argument parsing because the arguments are used in different passes,
 * but causes problems when printing documentation which matches all passes.
 *
 * NOTE(@ideasman42): Longer term we might want to avoid duplicating
 * arguments because it's confusing and not especially helpful.
 */
#define USE_DUPLICATE_ARG_WORKAROUND

static char NO_DOCS[] = "NO DOCUMENTATION SPECIFIED";

struct bArgDoc;
struct bArgDoc {
  bArgDoc *next, *prev;
  const char *short_arg;
  const char *long_arg;
  const char *documentation;
  bool done;
};

struct bAKey {
  const char *arg;
  uintptr_t pass; /* cast easier */
  int case_str;   /* case specific or not */
};

struct bArgument {
  bAKey *key;
  BA_ArgCallback func;
  void *data;
  bArgDoc *doc;
};

struct bArgs {
  ListBase docs;
  GHash *items;
  int argc;
  const char **argv;
  int *passes;
  /** For printing help text, defaults to `stdout`. */
  bArgPrintFn print_fn;
  void *print_user_data;

  /** Only use when initializing arguments. */
  int current_pass;
#ifdef USE_DUPLICATE_ARG_WORKAROUND
  /** The maximum pass (inclusive). */
  int pass_max;
#endif
};

static uint case_strhash(const void *ptr)
{
  const char *s = static_cast<const char *>(ptr);
  uint i = 0;
  uchar c;

  while ((c = tolower(*s++))) {
    i = i * 37 + c;
  }

  return i;
}

static uint keyhash(const void *ptr)
{
  const bAKey *k = static_cast<const bAKey *>(ptr);
  return case_strhash(k->arg); /* ^ BLI_ghashutil_inthash((void *)k->pass); */
}

static bool keycmp(const void *a, const void *b)
{
  const bAKey *ka = static_cast<const bAKey *>(a);
  const bAKey *kb = static_cast<const bAKey *>(b);
  if (ka->pass == kb->pass || ka->pass == -1 || kb->pass == -1) { /* -1 is wildcard for pass */
    if (ka->case_str == 1 || kb->case_str == 1) {
      return (BLI_strcasecmp(ka->arg, kb->arg) != 0);
    }
    return !STREQ(ka->arg, kb->arg);
  }
  return BLI_ghashutil_intcmp((const void *)ka->pass, (const void *)kb->pass);
}

static bArgument *lookUp(bArgs *ba, const char *arg, int pass, int case_str)
{
  bAKey key;

  key.case_str = case_str;
  key.pass = pass;
  key.arg = arg;

  return static_cast<bArgument *>(BLI_ghash_lookup(ba->items, &key));
}

/** Default print function. */
ATTR_PRINTF_FORMAT(2, 0)
static void args_print_wrapper(void * /*user_data*/, const char *format, va_list args)
{
  vprintf(format, args);
}

bArgs *BLI_args_create(int argc, const char **argv)
{
  bArgs *ba = MEM_callocN<bArgs>("bArgs");
  ba->passes = MEM_calloc_arrayN<int>(argc, "bArgs passes");
  ba->items = BLI_ghash_new(keyhash, keycmp, "bArgs passes gh");
  BLI_listbase_clear(&ba->docs);
  ba->argc = argc;
  ba->argv = argv;

  /* Must be initialized by #BLI_args_pass_set. */
  ba->current_pass = 0;
#ifdef USE_DUPLICATE_ARG_WORKAROUND
  ba->pass_max = 0;
#endif

  BLI_args_print_fn_set(ba, args_print_wrapper, nullptr);

  return ba;
}

void BLI_args_destroy(bArgs *ba)
{
  BLI_ghash_free(ba->items, MEM_freeN, MEM_freeN);
  MEM_freeN(ba->passes);
  BLI_freelistN(&ba->docs);
  MEM_freeN(ba);
}

void BLI_args_printf(bArgs *ba, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  ba->print_fn(ba->print_user_data, format, args);
  va_end(args);
}

void BLI_args_print_fn_set(bArgs *ba, bArgPrintFn print_fn, void *user_data)
{
  ba->print_fn = print_fn;
  ba->print_user_data = user_data;
}

void BLI_args_pass_set(bArgs *ba, int current_pass)
{
  BLI_assert((current_pass != 0) && (current_pass >= -1));
  ba->current_pass = current_pass;

#ifdef USE_DUPLICATE_ARG_WORKAROUND
  ba->pass_max = std::max(ba->pass_max, current_pass);
#endif
}

void BLI_args_print(const bArgs *ba)
{
  int i;
  for (i = 0; i < ba->argc; i++) {
    printf("argv[%d] = %s\n", i, ba->argv[i]);
  }
}

static bArgDoc *internalDocs(bArgs *ba,
                             const char *short_arg,
                             const char *long_arg,
                             const char *doc)
{
  bArgDoc *d;

  d = MEM_callocN<bArgDoc>("bArgDoc");

  if (doc == nullptr) {
    doc = NO_DOCS;
  }

  d->short_arg = short_arg;
  d->long_arg = long_arg;
  d->documentation = doc;

  BLI_addtail(&ba->docs, d);

  return d;
}

static void internalAdd(
    bArgs *ba, const char *arg, int case_str, BA_ArgCallback cb, void *data, bArgDoc *d)
{
  const int pass = ba->current_pass;
  bArgument *a;
  bAKey *key;

  a = lookUp(ba, arg, pass, case_str);

  if (a) {
    printf("WARNING: conflicting argument\n");
    printf("\ttrying to add '%s' on pass %i, %scase sensitive\n",
           arg,
           pass,
           case_str == 1 ? "not " : "");
    printf("\tconflict with '%s' on pass %i, %scase sensitive\n\n",
           a->key->arg,
           int(a->key->pass),
           a->key->case_str == 1 ? "not " : "");
  }

  a = MEM_callocN<bArgument>("bArgument");
  key = MEM_callocN<bAKey>("bAKey");

  key->arg = arg;
  key->pass = pass;
  key->case_str = case_str;

  a->key = key;
  a->func = cb;
  a->data = data;
  a->doc = d;

  BLI_ghash_insert(ba->items, key, a);
}

void BLI_args_add_case(bArgs *ba,
                       const char *short_arg,
                       int short_case,
                       const char *long_arg,
                       int long_case,
                       const char *doc,
                       BA_ArgCallback cb,
                       void *data)
{
  bArgDoc *d = internalDocs(ba, short_arg, long_arg, doc);

  if (short_arg) {
    internalAdd(ba, short_arg, short_case, cb, data, d);
  }

  if (long_arg) {
    internalAdd(ba, long_arg, long_case, cb, data, d);
  }
}

void BLI_args_add(bArgs *ba,
                  const char *short_arg,
                  const char *long_arg,
                  const char *doc,
                  BA_ArgCallback cb,
                  void *data)
{
  BLI_args_add_case(ba, short_arg, 0, long_arg, 0, doc, cb, data);
}

static void internalDocPrint(bArgs *ba, bArgDoc *d)
{
  if (d->short_arg && d->long_arg) {
    BLI_args_printf(ba, "%s or %s", d->short_arg, d->long_arg);
  }
  else if (d->short_arg) {
    BLI_args_printf(ba, "%s", d->short_arg);
  }
  else if (d->long_arg) {
    BLI_args_printf(ba, "%s", d->long_arg);
  }

  BLI_args_printf(ba, " %s\n\n", d->documentation);
}

void BLI_args_print_arg_doc(bArgs *ba, const char *arg)
{
#ifdef USE_DUPLICATE_ARG_WORKAROUND
  bArgument *a = nullptr;
  /* Find the first argument which has not been printed. */
  for (int pass = 0; pass <= ba->pass_max; pass++) {
    a = lookUp(ba, arg, pass, -1);
    if (a != nullptr) {
      const bArgDoc *d = a->doc;
      if (d->done == true) {
        a = nullptr;
      }
    }
    if (a) {
      break;
    }
  }
#else
  bArgument *a = lookUp(ba, arg, -1, -1);
#endif

  if (a) {
    bArgDoc *d = a->doc;

    internalDocPrint(ba, d);

    d->done = true;
  }
}

void BLI_args_print_other_doc(bArgs *ba)
{
  LISTBASE_FOREACH (bArgDoc *, d, &ba->docs) {
    if (d->done == 0) {
      internalDocPrint(ba, d);
    }
  }
}

bool BLI_args_has_other_doc(const bArgs *ba)
{
  LISTBASE_FOREACH (const bArgDoc *, d, &ba->docs) {
    if (d->done == 0) {
      return true;
    }
  }
  return false;
}

void BLI_args_parse(bArgs *ba, int pass, BA_ArgCallback default_cb, void *default_data)
{
  BLI_assert((pass != 0) && (pass >= -1));
  int i = 0;

  for (i = 1; i < ba->argc; i++) { /* skip argv[0] */
    if (ba->passes[i] == 0) {
      /* -1 signal what side of the comparison it is */
      bArgument *a = lookUp(ba, ba->argv[i], pass, -1);
      BA_ArgCallback func = nullptr;
      void *data = nullptr;

      if (a) {
        func = a->func;
        data = a->data;
      }
      else {
        func = default_cb;
        data = default_data;
      }

      if (func) {
        int retval = func(ba->argc - i, ba->argv + i, data);

        if (retval >= 0) {
          int j;

          /* use extra arguments */
          for (j = 0; j <= retval; j++) {
            ba->passes[i + j] = pass;
          }
          i += retval;
        }
        else if (retval == -1) {
          if (a) {
            if (a->key->pass != -1) {
              ba->passes[i] = pass;
            }
          }
          break;
        }
      }
    }
  }
}

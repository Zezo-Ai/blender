# SPDX-FileCopyrightText: 2002-2022 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(INC
  PUBLIC .
)

set(INC_SYS

)

set(SRC
  clog.cc

  CLG_log.h
)

set(LIB
  PRIVATE bf::intern::guardedalloc
  PRIVATE bf::intern::atomic
)

# Disabled for `makesdna` & `makesrna`.
add_definitions(-DWITH_CLOG_PTHREADS)

blender_add_lib(bf_intern_clog "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
add_library(bf::intern::clog ALIAS bf_intern_clog)

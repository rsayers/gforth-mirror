/*
  This is the machine-specific part for the AMD64 (n�e x86-64) architecture.

  Copyright (C) 1995,1996,1997,1998,2000,2003,2004 Free Software Foundation, Inc.

  This file is part of Gforth.

  Gforth is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
*/

#ifndef THREADING_SCHEME
#define THREADING_SCHEME 8
#endif

#if !defined(USE_TOS) && !defined(USE_NO_TOS)
#define USE_TOS
#endif

#ifndef USE_FTOS
#ifndef USE_NO_FTOS
#if defined(FORCE_REG)
#define USE_FTOS
#else
#define USE_NO_FTOS
#endif
#endif
#endif

#include "../generic/machine.h"

/* The architecture requires hardware consistency */
#define FLUSH_ICACHE(addr,size)

#if defined(FORCE_REG) && !defined(DOUBLY_INDIRECT) && !defined(VM_PROFILING)
#define RPREG asm("%r13")
#define FPREG asm("%r12")
#define TOSREG asm("%r14")
#define SPREG asm("%r15")
#define IPREG asm("%rbx")
#if 0
#define LPREG asm("%rbp") /* doesn't work now */
#endif
#define FTOSREG asm("%xmm8")
#endif

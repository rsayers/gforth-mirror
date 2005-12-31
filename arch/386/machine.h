/*
  This is the machine-specific part for Intel 386 compatible processors

  Copyright (C) 1995,1996,1997,1998,2000,2003,2004,2005 Free Software Foundation, Inc.

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

#ifdef HAVE_LIBKERNEL32
#ifdef i386
#define SYSCALL     __attribute__ ((stdcall))
#endif
#endif

#if (((__GNUC__==2 && defined(__GNUC_MINOR__) && __GNUC_MINOR__>=95) || (__GNUC__>2))) && defined(FORCE_REG)
#if !defined(USE_TOS) && !defined(USE_NO_TOS)
#define USE_TOS
#endif
#endif
#define MEMCMP_AS_SUBROUTINE 1

#ifndef USE_FTOS
#ifndef USE_NO_FTOS
/* gcc can't keep TOS in a register on the 386, so don't try */
#define USE_NO_FTOS
#endif
#endif

#define ASM_SM_SLASH_REM4(d1, n1, n2, n3) \
	asm("idivl %3": "=a"(n3),"=d"(n2) : "A"(d1),"g"(n1):"cc");

#define ASM_UM_SLASH_MOD4(d1, n1, n2, n3) \
	asm("divl %3": "=a"(n3),"=d"(n2) : "A"(d1),"g"(n1):"cc");

#include "../generic/machine.h"

/* 386 and below have no cache, 486 has a shared cache, and the
   Pentium and later employ hardware cache consistency, so
   flush-icache is a noop */
#define FLUSH_ICACHE(addr,size)

#if defined(FORCE_REG) && !defined(DOUBLY_INDIRECT) && !defined(VM_PROFILING)
#if (__GNUC__==2 && defined(__GNUC_MINOR__) && __GNUC_MINOR__==5)
/* i.e. gcc-2.5.x */
/* this works with 2.5.7; nothing works with 2.5.8 */
#define IPREG asm("%esi")
#define SPREG asm("%edi")
#if 0
#ifdef USE_TOS
#define CFAREG asm("%ecx")
#else
#define CFAREG asm("%edx")
#endif
#endif
#else /* !gcc-2.5.x */
/* this works with 2.6.3 (and quite well, too) */
/* since this is not very demanding, it's the default for other gcc versions */
#if defined(USE_TOS) && !defined(CFA_NEXT)
#if ((__GNUC__==2 && defined(__GNUC_MINOR__) && __GNUC_MINOR__>=95) || (__GNUC__>2))
     /* gcc 2.95 has a better register allocater */
#define SPREG asm("%esi")
#define RPREG asm("%edi")
#ifdef NO_IP
#define spbREG asm("%ebx")
#else
#define IPREG asm("%ebx")
#endif
/* ebp leads to broken code (gcc-3.0); eax, ecx, edx produce compile errors */
#define TOSREG asm("%ecx")
/* ecx works only for TOS, and eax, edx don't work for anything (gcc-3.0) */
#else /* !(gcc-2.95 or later) */
#define IPREG asm("%ebx")
#endif /* !(gcc-2.95 or later) */
#else /* !defined(USE_TOS) || defined(CFA_NEXT) */
#if ((__GNUC__==2 && defined(__GNUC_MINOR__) && __GNUC_MINOR__>=95) || (__GNUC__>2))
#define SPREG asm("%esi")
#define RPREG asm("%edi")
#ifdef NO_IP
#define spbREG asm("%ebx")
#else
#define IPREG asm("%ebx")
#endif
#else /* !(gcc-2.95 or later) */
#define SPREG asm("%ebx")
#endif  /* !(gcc-2.95 or later) */
#endif /* !defined(USE_TOS) || defined(CFA_NEXT) */
#endif /* !gcc-2.5.x */
#endif /* defined(FORCE_REG) && !defined(DOUBLY_INDIRECT) && !defined(VM_PROFILING) */

/* #define ALIGNMENT_CHECK 1 */

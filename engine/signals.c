/* signal handling

  Copyright (C) 1995,1996,1997,1998 Free Software Foundation, Inc.

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
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/


#define _GNU_SOURCE

#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#if !defined(apollo) && !defined(MSDOS)
#include <sys/ioctl.h>
#endif
/* #include <asm/signal.h> */
#include <signal.h>
#include "config.h"
#include "forth.h"
#include "io.h"


#define DEFAULTCOLS 80
#if defined(MSDOS) || defined (_WIN32)
#define DEFAULTROWS 25
#else
#define DEFAULTROWS 24
#endif

UCell cols=DEFAULTCOLS;
UCell rows=DEFAULTROWS;


static void
graceful_exit (int sig)
{
  deprep_terminal();
  fprintf (stderr, "\n\n%s.\n", strsignal (sig));
  exit (0x80|sig);
}

jmp_buf throw_jmp_buf;

static void 
signal_throw(int sig)
{
  int code;

  switch (sig) {
  case SIGINT: code=-28; break;
  case SIGFPE: code=-55; break;
#ifdef SIGBUS
  case SIGBUS: code=-23; break;
#endif
  case SIGSEGV: code=-9; break;
  default: code=-256-sig; break;
  }
  longjmp(throw_jmp_buf,code); /* !! or use siglongjmp ? */
}

#ifdef SA_SIGINFO
static void fpe_handler(int sig, siginfo_t *info, void *_)
     /* handler for SIGFPE */
{
  int code;

  switch(info->si_code) {
  case FPE_INTDIV: code=-10; break; /* integer divide by zero */
  case FPE_INTOVF: code=-11; break; /* integer overflow */
  case FPE_FLTDIV: code=-42; break; /* floating point divide by zero */
  case FPE_FLTOVF: code=-43; break; /* floating point overflow  */
  case FPE_FLTUND: code=-54; break; /* floating point underflow  */
  case FPE_FLTRES: code=-41; break; /* floating point inexact result  */
  case FPE_FLTINV: /* invalid floating point operation  */
  case FPE_FLTSUB: /* subscript out of range  */
  default: code=-55; break;
  }
  longjmp(throw_jmp_buf,code);
}


#define SPILLAGE 128
/* if there's a SIGSEGV within SPILLAGE bytes of some stack, we assume
   that this stack has over/underflowed */

#define JUSTUNDER(addr1,addr2) (((UCell)((addr2)-1-(addr1)))<SPILLAGE)
/* true is addr1 is just under addr2 */

#define JUSTOVER(addr1,addr2) (((UCell)((addr1)-(addr2)))<SPILLAGE)

#define NEXTPAGE(addr) ((Address)((((UCell)(addr)-1)&-pagesize)+pagesize))

static void segv_handler(int sig, siginfo_t *info, void *_)
{
  int code=-9;
  Address addr=info->si_addr;
  ImageHeader *h=gforth_header;

  if (JUSTUNDER(addr, h->data_stack_base))
    code=-3;
  else if (JUSTOVER(addr, NEXTPAGE(h->data_stack_base+h->data_stack_size)))
    code=-4;
  else if (JUSTUNDER(addr, h->return_stack_base))
    code=-5;
  else if (JUSTOVER(addr, NEXTPAGE(h->return_stack_base+h->return_stack_size)))
    code=-6;
  else if (JUSTUNDER(addr, h->fp_stack_base))
    code=-44;
  else if (JUSTOVER(addr, NEXTPAGE(h->fp_stack_base+h->fp_stack_size)))
    code=-45;
  longjmp(throw_jmp_buf,code);
}

#endif /* defined(SA_SIGINFO) */

#ifdef SIGCONT
static void termprep (int sig)
{
  signal(sig,termprep);
  terminal_prepped=0;
}
#endif

void get_winsize()
{
#ifdef TIOCGWINSZ
  struct winsize size;
  
  if (ioctl (1, TIOCGWINSZ, (char *) &size) >= 0) {
    rows = size.ws_row;
    cols = size.ws_col;
  }
#else
  char *s;
  if ((s=getenv("LINES"))) {
    rows=atoi(s);
    if (rows==0)
      rows=DEFAULTROWS;
  }
  if ((s=getenv("COLUMNS"))) {
    rows=atoi(s);
    if (rows==0)
      cols=DEFAULTCOLS;
  }
#endif
}

#ifdef SIGWINCH
static void change_winsize(int sig)
{
  signal(sig,change_winsize);
#ifdef TIOCGWINSZ
  get_winsize();
#endif
}
#endif


#ifdef SA_SIGINFO
void install_signal_handler(int sig, void (*handler)(int, siginfo_t *, void *))
     /* installs three-argument signal handler for sig */
{
  struct sigaction action;

  action.sa_sigaction=handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags=SA_RESTART|SA_SIGINFO; /* pass siginfo */
  sigaction(sig, &action, NULL);
}
#endif

void install_signal_handlers(void)
{

#if 0
/* these signals are handled right by default, no need to handle them;
   they are listed here just for fun */
  static short sigs_to_default [] = {
#ifdef SIGCHLD
    SIGCHLD,
#endif
#ifdef SIGINFO
    SIGINFO,
#endif
#ifdef SIGIO
    SIGIO,
#endif
#ifdef SIGLOST
    SIGLOST,
#endif
#ifdef SIGKILL
    SIGKILL,
#endif
#ifdef SIGSTOP
    SIGSTOP,
#endif
#ifdef SIGPWR
    SIGPWR,
#endif
#ifdef SIGMSG
    SIGMSG,
#endif
#ifdef SIGDANGER
    SIGDANGER,
#endif
#ifdef SIGMIGRATE
    SIGMIGRATE,
#endif
#ifdef SIGPRE
    SIGPRE,
#endif
#ifdef SIGVIRT
    SIGVIRT,
#endif
#ifdef SIGGRANT
    SIGGRANT,
#endif
#ifdef SIGRETRACT
    SIGRETRACT,
#endif
#ifdef SIGSOUND
    SIGSOUND,
#endif
#ifdef SIGSAK
    SIGSAK,
#endif
#ifdef SIGTSTP
    SIGTSTP,
#endif
#ifdef SIGTTIN
    SIGTTIN,
#endif
#ifdef SIGTTOU
    SIGTTOU,
#endif
#ifdef SIGSTKFLT
    SIGSTKFLT,
#endif
#ifdef SIGUNUSED
    SIGUNUSED,
#endif
  };
#endif

  static short sigs_to_throw [] = {
#ifdef SIGBREAK
    SIGBREAK,
#endif
#ifdef SIGINT
    SIGINT,
#endif
#ifdef SIGILL
    SIGILL,
#endif
#ifdef SIGEMT
    SIGEMT,
#endif
#ifdef SIGFPE
    SIGFPE,
#endif
#ifdef SIGIOT
    SIGIOT,
#endif
#ifdef SIGSEGV
    SIGSEGV,
#endif
#ifdef SIGALRM
    SIGALRM,
#endif
#ifdef SIGPIPE
    SIGPIPE,
#endif
#ifdef SIGPOLL
    SIGPOLL,
#endif
#ifdef SIGPROF
    SIGPROF,
#endif
#ifdef SIGBUS
    SIGBUS,
#endif
#ifdef SIGSYS
    SIGSYS,
#endif
#ifdef SIGTRAP
    SIGTRAP,
#endif
#ifdef SIGURG
    SIGURG,
#endif
#ifdef SIGUSR1
    SIGUSR1,
#endif
#ifdef SIGUSR2
    SIGUSR2,
#endif
#ifdef SIGVTALRM
    SIGVTALRM,
#endif
#ifdef SIGXFSZ
    SIGXFSZ,
#endif
  };
  static short sigs_to_quit [] = {
#ifdef SIGHUP
    SIGHUP,
#endif
#ifdef SIGQUIT
    SIGQUIT,
#endif
#ifdef SIGABRT
    SIGABRT,
#endif
#ifdef SIGTERM
    SIGTERM,
#endif
#ifdef SIGXCPU
    SIGXCPU,
#endif
  };
  int i;
  void (*throw_handler)() = die_on_signal ? graceful_exit : signal_throw;

#define DIM(X)		(sizeof (X) / sizeof *(X))
/*
  for (i = 0; i < DIM (sigs_to_ignore); i++)
    signal (sigs_to_ignore [i], SIG_IGN);
*/
  for (i = 0; i < DIM (sigs_to_throw); i++)
    signal(sigs_to_throw[i], throw_handler);
  for (i = 0; i < DIM (sigs_to_quit); i++)
    signal (sigs_to_quit [i], graceful_exit);
#ifdef SA_SIGINFO
  install_signal_handler(SIGFPE, fpe_handler);
  install_signal_handler(SIGSEGV, segv_handler);
#endif
#ifdef SIGCONT
    signal (SIGCONT, termprep);
#endif
#ifdef SIGWINCH
    signal (SIGWINCH, change_winsize);
#endif
}
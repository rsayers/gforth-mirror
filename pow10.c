/* a simple pow10 implementation

  Copyright (C) 1995 Free Software Foundation, Inc.

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
#include <math.h>

#ifndef M_LN10
#define M_LN10      2.30258509299404568402
#endif

/* this should be defined by math.h; If it is not, the miranda
   prototype would be wrong; Since we prefer compile-time errors to
   run-time errors, it's declared here. */
extern double exp(double);

double pow10(double x)
{
  return exp(x*M_LN10);
}

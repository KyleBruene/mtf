/*

mtf - a Microsoft Tape Format reader (and future writer?)
Copyright (C) 1999  D. Alan Stewart, Layton Graphics, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

Contact the author at:

D. Alan Stewart
Layton Graphics, Inc.
155 Woolco Dr.
Marietta, GA 30062, USA
astewart@layton-graphics.com

See mtf.c for version history, contributors, etc.

**
**	mtfutil.c
**
**	mtf utility functions
**
*/


#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/fcntl.h>
#include <limits.h>
#include "mtf.h"


extern UINT16 remaining;
extern UINT8 *tBuffer;


/* strlwr() lowercases a string.                                              */

void strlwr(char* str)
{
	char *ptr;

	ptr = str;
	while (*ptr != '\0')
	{
		if ((*ptr >= 'A') && (*ptr <= 'Z'))
			*ptr += 32;
		
		ptr += 1;
	}

	return;
}


/* strupr() uppercases a string.                                              */

void strupr(char* str)
{
	char *ptr;

	ptr = str;
	while (*ptr != '\0')
	{
		if ((*ptr >= 'a') && (*ptr <= 'z'))
			*ptr -= 32;
		
		ptr += 1;
	}

	return;
}



/* increment64() adds a value to a UINT64 structure. There is no error        */
/* checking.                                                                  */

void increment64(UINT64 *big, UINT32 inc)
{
	if (ULONG_MAX - inc <= big->least)
	{
		big->most += 1;
		big->least = inc - (ULONG_MAX - big->least);
	}
	else
	{
		big->least += inc;
	}

	return;
}
		

/* increment64() subtracts a value from a UINT64 structure. There is no error */
/* checking.                                                                  */

void decrement64(UINT64 *big, UINT32 dec)
{
	if (dec > big->least)
	{
		big->most -= 1;
		big->least = ULONG_MAX - (dec - big->least);
	}
	else
	{
		big->least -= dec;
	}

	return;
}


void dump(char *name)
{
	int handle;

	handle = open(name, O_WRONLY | O_TRUNC | O_CREAT);
	if (handle != -1)
	{
		write(handle, tBuffer, remaining);
		close(handle);
	}

	return;
}

/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Title: POSIX Host Time Functions
**  Purpose:
**      Provide platform support for times and timing information.
**
***********************************************************************/

#ifndef __cplusplus
	// See feature_test_macros(7)
	// This definition is redundant under C++
	#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <poll.h>
#include <fcntl.h>              /* Obtain O_* constant definitions */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "reb-host.h"


#ifndef timeval // for older systems
#include <sys/time.h>
#endif

/***********************************************************************
**
*/	static int Get_Timezone(struct tm *local_tm)
/*
**		Get the time zone in minutes from GMT.
**		NOT consistently supported in Posix OSes!
**		We have to use a few different methods.
**
**		!!! "local_tm->tm_gmtoff / 60 would make the most sense,
**		but is no longer used" (said a comment)
**
***********************************************************************/
{
#ifdef HAS_SMART_TIMEZONE
	time_t rightnow;
	time(&rightnow);
	return cast(int,
		difftime(mktime(localtime(&rightnow)), mktime(gmtime(&rightnow))) / 60
	);
#else
	struct tm tm2;
	time_t rightnow;
	time(&rightnow);
	tm2 = *localtime(&rightnow);
	tm2.tm_isdst=0;
	return (int)difftime(mktime(&tm2), mktime(gmtime(&rightnow))) / 60;
#endif
}


/***********************************************************************
**
*/	void Convert_Date(time_t *stime, REBOL_DAT *dat, long zone)
/*
**		Convert local format of system time into standard date
**		and time structure (for date/time and file timestamps).
**
***********************************************************************/
{
	struct tm *time;

	CLEARS(dat);

	time = gmtime(stime);

	dat->year  = time->tm_year + 1900;
	dat->month = time->tm_mon + 1;
	dat->day   = time->tm_mday;
	dat->time  = time->tm_hour * 3600 + time->tm_min * 60 + time->tm_sec;
	dat->nano  = 0;
	dat->zone  = Get_Timezone(time);
}


/***********************************************************************
**
*/	void OS_Get_Time(REBOL_DAT *dat)
/*
**		Get the current system date/time in UTC plus zone offset (mins).
**
***********************************************************************/
{
	struct timeval tv;
	time_t stime;

	gettimeofday(&tv, 0); // (tz field obsolete)
	stime = tv.tv_sec;
	Convert_Date(&stime, dat, -1);
	dat->nano  = tv.tv_usec * 1000;
}


/***********************************************************************
**
*/	i64 OS_Delta_Time(i64 base, int flags)
/*
**		Return time difference in microseconds. If base = 0, then
**		return the counter. If base != 0, compute the time difference.
**
**		NOTE: This needs to be precise, but many OSes do not
**		provide a precise time sampling method. So, if the target
**		posix OS does, add the ifdef code in here.
**
***********************************************************************/
{
	struct timeval tv;
	i64 time;

	gettimeofday(&tv,0);

	time = cast(i64, tv.tv_sec * 1000000) + tv.tv_usec;

	if (base == 0) return time;

	return time - base;
}


/***********************************************************************
**
*/	void OS_File_Time(REBREQ *file, REBOL_DAT *dat)
/*
**		Convert file.time to REBOL date/time format.
**		Time zone is UTC.
**
***********************************************************************/
{
	if (sizeof(time_t) > sizeof(file->special.file.time.l)) {
		REBI64 t = file->special.file.time.l;
		t |= cast(REBI64, file->special.file.time.h) << 32;
		Convert_Date(cast(time_t*, &t), dat, 0);
	} else {
		Convert_Date(cast(time_t *, &file->special.file.time.l), dat, 0);
	}
}


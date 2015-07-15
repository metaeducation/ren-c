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
**  Title: Browser Launch Host
**  Purpose:
**		This provides the ability to launch a web browser or file
**		browser on the host.
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


#ifndef PATH_MAX
#define PATH_MAX 4096  // generally lacking in Posix
#endif

void OS_Destroy_Graphics(void);


/***********************************************************************
**
*/	int OS_Get_Current_Dir(REBCHR **path)
/*
**		Return the current directory path as a string and
**		its length in chars (not bytes).
**
**		The result should be freed after copy/conversion.
**
***********************************************************************/
{
	*path = OS_ALLOC_ARRAY(char, PATH_MAX);
	if (!getcwd(*path, PATH_MAX-1)) *path[0] = 0;
	return strlen(*path);
}


/***********************************************************************
**
*/	REBOOL OS_Set_Current_Dir(REBCHR *path)
/*
**		Set the current directory to local path. Return FALSE
**		on failure.
**
***********************************************************************/
{
	return chdir(path) == 0;
}


/***********************************************************************
**
*/	REBOOL OS_Request_File(REBRFR *fr)
/*
***********************************************************************/
{
	return FALSE;
}


/***********************************************************************
**
*/	REBOOL OS_Request_Dir(REBCHR* title, REBCHR** folder, REBCHR* path)
/*
**	WARNING: TEMPORARY implementation! Used only by host-core.c
**  Will be most probably changed in future.
**
***********************************************************************/
{
	return FALSE;
}


static int Try_Browser(char *browser, REBCHR *url)
{
	pid_t pid;
	int result, status;

	switch (pid = fork()) {
		case -1:
			result = FALSE;
			break;
		case 0:
			execlp(browser, browser, url, NULL);
			exit(1);
			break;
		default:
			waitpid(pid, &status, WUNTRACED);
			result = WIFEXITED(status)
					&& (WEXITSTATUS(status) == 0);
	}

	return result;
}


/***********************************************************************
**
*/	int OS_Browse(const REBCHR *url, int reserved)
/*
***********************************************************************/
{
	if (Try_Browser("/usr/bin/open", url))
		return TRUE;
	return FALSE;
}


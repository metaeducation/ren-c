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

#ifdef USE_GTK_FILECHOOSER
int os_create_file_selection (void 			*libgtk,
							  char 			*buf,
							  int 			len,
							  const char 	*title,
							  const char 	*path,
							  int 			save,
							  int 			multiple);

int os_init_gtk(void *libgtk);
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
	REBOOL ret = FALSE;
#ifdef USE_GTK_FILECHOOSER
	REBINT error;
	const char * libs [] = {
		"libgtk-3.so",
		"libgtk-3.so.0", /* Some systems, like Ubuntu, don't have libgtk-3.so */
		NULL
	};
	const char **ptr = NULL;
	void *libgtk = NULL;
	for (ptr = &libs[0]; *ptr != NULL; ptr ++) {
		libgtk = OS_Open_Library(*ptr, &error);
		if (libgtk != NULL) {
			break;
		}
	}

	if (libgtk == NULL) {
		//RL_Print("open libgtk-3.so failed: %s\n", dlerror());
		return FALSE;
	}
	if (!os_init_gtk(libgtk)) {
		//RL_Print("init gtk failed\n");
		OS_Close_Library(libgtk);
		return FALSE;
	}
	if (os_create_file_selection(libgtk,
								 fr->files,
								 fr->len,
								 fr->title,
								 fr->dir,
								 GET_FLAG(fr->flags, FRF_SAVE),
								 GET_FLAG(fr->flags, FRF_MULTI))) {
		//RL_Print("file opened returned\n");
		ret = TRUE;
	}
	OS_Close_Library(libgtk);
	return ret;
#else
	return ret;
#endif
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

// These are repeated several times; there needs to be a way for the host
// to define things in the API without adding directly to make-host-ext.r

#define INHERIT_TYPE 0
#define NONE_TYPE 1
#define STRING_TYPE 2
#define FILE_TYPE 3
#define BINARY_TYPE 4

#define FLAG_WAIT 1
#define FLAG_CONSOLE 2
#define FLAG_SHELL 4
#define FLAG_INFO 8


static int Try_Browser(const char *browser, const REBCHR *url)
{
	// Must be compile-time const for '= {...}' style init (-Wc99-extensions)
	const char *argv[3];

	argv[0] = browser;
	argv[1] = url;
	argv[2] = NULL;

	return OS_Create_Process(browser, 2, argv, 0,
							NULL, /* pid */
							NULL, /* exit_code */
							INHERIT_TYPE, NULL, 0, /* input_type, void *input, u32 input_len, */
							INHERIT_TYPE, NULL, NULL, /* output_type, void **output, u32 *output_len, */
							INHERIT_TYPE, NULL, NULL); /* u32 err_type, void **err, u32 *err_len */
}

/***********************************************************************
**
*/	int OS_Browse(const REBCHR *url, int reserved)
/*
***********************************************************************/
{
	return Try_Browser("xdg-open", url) && Try_Browser("x-www-browser", url);
}

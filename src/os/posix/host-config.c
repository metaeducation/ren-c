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
**  Title: POSIX Host Configuration Routines
**  Purpose:
**		This file is for situations where there is some kind of
**		configuration information (e.g. environment variables, boot
**		paths) that Rebol wants to get at from the host.
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

extern char **environ;

#include "reb-host.h"


/***********************************************************************
**
*/	REBINT OS_Config(int id, REBYTE *result)
/*
**		Return a specific runtime configuration parameter.
**
***********************************************************************/
{
#define OCID_STACK_SIZE 1  // needs to move to .h file

	switch (id) {
	case OCID_STACK_SIZE:
		return 0;  // (size in bytes should be returned here)
	}

	return 0;
}


/***********************************************************************
**
*/	REBOOL OS_Get_Boot_Path(REBCHR *name)
/*
**		Used to determine the program file path for REBOL.
**		This is the path stored in system->options->boot and
**		it is used for finding default boot files.
**
***********************************************************************/
{
	return FALSE; // not yet used
}


/***********************************************************************
**
*/	REBINT OS_Get_Env(REBCHR *envname, REBCHR* envval, REBINT valsize)
/*
**		Get a value from the environment.
**		Returns size of retrieved value for success or zero if missing.
**		If return size is greater than valsize then value contents
**		are undefined, and size includes null terminator of needed buf
**
***********************************************************************/
{
	// Note: The Posix variant of this API is case-sensitive

	REBINT len;
	const char* value = getenv(envname);
	if (value == 0) return 0;

	len = strlen(value);
	if (len == 0) return -1; // shouldn't have saved an empty env string

	if (len + 1 > valsize) {
		return len + 1;
	}

	strncpy(envval, value, len);
	return len;
}


/***********************************************************************
**
*/	REBOOL OS_Set_Env(REBCHR *envname, REBCHR *envval)
/*
**		Set a value from the environment.
**		Returns >0 for success and 0 for errors.
**
***********************************************************************/
{
	if (envval) {
#ifdef setenv
		// we pass 1 for overwrite (make call to OS_Get_Env if you
		// want to check if already exists)

		if (setenv(envname, envval, 1) == -1)
			return FALSE;
#else
		// WARNING: KNOWN MEMORY LEAK!

		// putenv is *fatally flawed*, and was obsoleted by setenv
		// and unsetenv System V...

		// http://stackoverflow.com/a/5876818/211160

		// once you have passed a string to it you never know when that
		// string will no longer be needed.  Thus it may either not be
		// dynamic or you must leak it, or track a local copy of the
		// environment yourself.

		// If you're stuck without setenv on some old platform, but
		// really need to set an environment variable, here's a way
		// that just leaks a string each time you call.

		char *expr = OS_ALLOC_ARRAY(char,
			strlen(envname) + 1 + strlen(envval) + 1
		);

		strcpy(expr, envname);
		strcat(expr, "=");
		strcat(expr, envval);

		if (putenv(expr) == -1)
			return FALSE;
#endif
		return TRUE;
	}

#ifdef unsetenv
	if (unsetenv(envname) == -1)
		return FALSE;
#else
	// WARNING: KNOWN PORTABILITY ISSUE

	// Simply saying putenv("FOO") will delete FOO from
	// the environment, but it's not consistent...does
	// nothing on NetBSD for instance.  But not all
	// other systems have unsetenv...
	//
	// http://julipedia.meroh.net/2004/10/portability-unsetenvfoo-vs-putenvfoo.html

	// going to hope this case doesn't hold onto the string...
	if (putenv((char*)envname) == -1)
		return FALSE;
#endif
	return TRUE;
}


/***********************************************************************
**
*/	REBCHR *OS_List_Env(void)
/*
***********************************************************************/
{
	int n, len = 0;
	char *str, *cp;

	// compute total size:
	// Note: 'environ' is an extern of a global found in <unistd.h>
	for (n = 0; environ[n]; n++) len += 1 + strlen(environ[n]);

	cp = str = OS_ALLOC_ARRAY(char, len + 1); // +terminator
	*cp = 0;

	// combine all strings into one:
	for (n = 0; environ[n]; n++) {
		len = strlen(environ[n]);
		strcat(cp, environ[n]);
		cp += len;
		*cp++ = 0;
		*cp = 0;
	}

	return str; // caller will free it
}

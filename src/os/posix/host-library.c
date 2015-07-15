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
**  Title: POSIX Library-related functions
**  Purpose:
**		This is for support of the LIBRARY! type from the host on
**		systems that support 'dlopen'.
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


#ifndef NO_DL_LIB
#include <dlfcn.h>
#endif


/***********************************************************************
**
*/	void *OS_Open_Library(const REBCHR *path, REBCNT *error)
/*
**		Load a DLL library and return the handle to it.
**		If zero is returned, error indicates the reason.
**
***********************************************************************/
{
#ifndef NO_DL_LIB
	void *dll = dlopen(path, RTLD_LAZY/*|RTLD_GLOBAL*/);
	if (error) {
		*error = 0; // dlerror() returns a char* error message, so there's
	}
				// no immediate way to return an "error code" in *error
	return dll;
#else
	return 0;
#endif
}


/***********************************************************************
**
*/	void OS_Close_Library(void *dll)
/*
**		Free a DLL library opened earlier.
**
***********************************************************************/
{
#ifndef NO_DL_LIB
	dlclose(dll);
#endif
}


/***********************************************************************
**
*/	CFUNC *OS_Find_Function(void *dll, const char *funcname)
/*
**		Get a DLL function address from its string name.
**
***********************************************************************/
{
#ifndef NO_DL_LIB
	// !!! See notes about data pointers vs. function pointers in the
	// definition of CFUNC.  This is trying to stay on the right side
	// of the specification, but OS APIs often are not standard C.  So
	// this implementation is not guaranteed to work, just to suppress
	// compiler warnings.  See:
	//
	//		http://stackoverflow.com/a/1096349/211160

	CFUNC *fp;
	*cast(void**, &fp) = dlsym(dll, funcname);
	return fp;
#else
	return NULL;
#endif
}

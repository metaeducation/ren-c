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
**  Title: POSIX Exit and Error Functions
**  Purpose:
**		...
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


/***********************************************************************
**
*/	void OS_Exit(int code)
/*
**		Called in cases where REBOL needs to quit immediately
**		without returning from the main() function.
**
***********************************************************************/
{
	//OS_Call_Device(RDI_STDIO, RDC_CLOSE); // close echo
	OS_Quit_Devices(0);
#ifndef REB_CORE
	OS_Destroy_Graphics();
#endif
	exit(code);
}

static const void * backtrace_buf [1024];
/***********************************************************************
**
** coverity[+kill]
*/	void OS_Crash(const REBYTE *title, const REBYTE *content)
/*
**		Tell user that REBOL has crashed. This function must use
**		the most obvious and reliable method of displaying the
**		crash message.
**
**		If the title is NULL, then REBOL is running in a server mode.
**		In that case, we do not want the crash message to appear on
**		the screen, because the system may be unattended.
**
**		On some systems, the error may be recorded in the system log.
**
***********************************************************************/
{
	// Echo crash message if echo file is open:
	///PUTE(content);
	OS_Call_Device(RDI_STDIO, RDC_CLOSE); // close echo

	// A title tells us we should alert the user:
	if (title) {
		fputs(cs_cast(title), stderr);
		fputs(":\n", stderr);
	}
	fputs(cs_cast(content), stderr);
	fputs("\n\n", stderr);
#ifdef backtrace  // A GNU extension
	fputs("Backtrace:\n", stderr);
	int n_backtrace = backtrace(backtrace_buf, sizeof(backtrace_buf)/sizeof(backtrace_buf[0]));
	backtrace_symbols_fd(backtrace_buf, n_backtrace, STDERR_FILENO);
#endif
	exit(100);
}


/***********************************************************************
**
*/	REBCHR *OS_Form_Error(int errnum, REBCHR *str, int len)
/*
**		Translate OS error into a string. The str is the string
**		buffer and the len is the length of the buffer.
**
***********************************************************************/
{
	// strerror() is not thread-safe, and there are two different protocols
	// for strerror_r(), depending on whether you are using an "XSI"
	// compliant implementation or GNU's implementation.  The convoluted
	// test below is the actual test recommended to decide which version
	// of strerror_r() you have.

#if (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && !defined(_GNU_SOURCE)
	// "The XSI-compliant strerror_r() function returns 0 on success.
	// On error, a (positive) error number is returned (since glibc 2.13),
	// or -1 is returned and errno is set to indicate the error (glibc
	// versions before 2.13)."

	int result = strerror_r(errnum, str, len);

	// Alert us to any problems in a debug build
	assert(result == 0);

	if (result == 0) {
		// success...
	}
	else if (result == EINVAL) {
		strncpy(str, "EINVAL: bad error num passed to strerror_r()", len);
	}
	else if (result == ERANGE) {
		strncpy(str, "ERANGE: insufficient size in buffer for error", len);
	}
	else {
		strncpy(str, "Unknown error while getting strerror_r() message", len);
	}
#else
	// May return an immutable string instead of filling the buffer
	char *maybe_str = strerror_r(errnum, str, len);
	if (maybe_str != str)
		strncpy(str, maybe_str, len);
#endif

	return str;
}

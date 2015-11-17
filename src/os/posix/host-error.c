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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_EXECINFO_AVAILABLE
	#include <execinfo.h>
	#include <unistd.h>  // STDERR_FILENO
#endif

#include "reb-host.h"


//
//  OS_Exit: C
// 
// Called in cases where REBOL needs to quit immediately
// without returning from the main() function.
//
void OS_Exit(int code)
{
	//OS_Call_Device(RDI_STDIO, RDC_CLOSE); // close echo
	OS_Quit_Devices(0);
#ifndef REB_CORE
	OS_Destroy_Graphics();
#endif
	exit(code);
}

//
//  OS_Crash: C
// 
// Tell user that REBOL has crashed. This function must use
// the most obvious and reliable method of displaying the
// crash message.
// 
// If the title is NULL, then REBOL is running in a server mode.
// In that case, we do not want the crash message to appear on
// the screen, because the system may be unattended.
// 
// On some systems, the error may be recorded in the system log.
//
// coverity[+kill]
//
void OS_Crash(const REBYTE *title, const REBYTE *content)
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

#ifdef HAVE_EXECINFO_AVAILABLE  // backtrace is a GNU extension.
	{
		void *backtrace_buf[1024];
		int n_backtrace = backtrace(backtrace_buf, sizeof(backtrace_buf)/sizeof(backtrace_buf[0]));
		fputs("Backtrace:\n", stderr);
		backtrace_symbols_fd(backtrace_buf, n_backtrace, STDERR_FILENO);
	}
#endif

	exit(EXIT_FAILURE);
}


//
//  OS_Form_Error: C
// 
// Translate OS error into a string. The str is the string
// buffer and the len is the length of the buffer.
//
REBCHR *OS_Form_Error(int errnum, REBCHR *str, int len)
{
	// strerror() is not thread-safe, but strerror_r is. Unfortunately, at
	// least in glibc, there are two different protocols for strerror_r(),
	// depending on whether you are using the POSIX-compliant
	// implementation or the GNU implementation. The convoluted test below
	// is the inversion of the actual test recommended by glibc to discern
	// the version of strerror_r() provided. As other, non-glibc
	// implementations (such as OS X's libSystem) also provide the
	// POSIX-compliant version, we invert the test: explicitly use the
	// older GNU implementation when we are sure about it, and use the
	// more modern POSIX-compliant version otherwise. Finally, we only
	// attempt this feature detection when using glibc (__GNU_LIBRARY__),
	// as this particular combination of the (more widely standardised)
	// _POSIX_C_SOURCE and _XOPEN_SOURCE defines might mean something
	// completely different on non-glibc implementations. (Note that
	// undefined pre-processor names arithmetically compare as 0, which is
	// used in the original glibc test; we are more explicit.)

#if defined(__GNU_LIBRARY__) \
		&& (defined(_GNU_SOURCE) \
			|| ((!defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200112L) \
				&& (!defined(_XOPEN_SOURCE) || _XOPEN_SOURCE < 600)))
	// May return an immutable string instead of filling the buffer
	char *maybe_str = strerror_r(errnum, str, len);
	if (maybe_str != str)
		strncpy(str, maybe_str, len);
#else
	// Quoting glibc's strerror_r manpage: "The XSI-compliant strerror_r()
	// function returns 0 on success. On error, a (positive) error number is
	// returned (since glibc 2.13), or -1 is returned and errno is set to
	// indicate the error (glibc versions before 2.13)."

	int result = strerror_r(errnum, str, len);

	// Alert us to any problems in a debug build.
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
#endif

	return str;
}

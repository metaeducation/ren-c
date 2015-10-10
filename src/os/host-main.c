/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Additional code modifications and improvements Copyright 2012 Saphirion AG
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
**	Title: Host environment main entry point
**	Note: OS independent
**  Author: Carl Sassenrath
**  Purpose:
**		Provides the outer environment that calls the REBOL lib.
**		This module is more or less just an example and includes
**		a very simple console prompt.
**
************************************************************************
**
**  WARNING to PROGRAMMERS:
**
**		This open source code is strictly managed to maintain
**		source consistency according to our standards, not yours.
**
**		1. Keep code clear and simple.
**		2. Document odd code, your reasoning, or gotchas.
**		3. Use our source style for code, indentation, comments, etc.
**		4. It must work on Win32, Linux, OS X, BSD, big/little endian.
**		5. Test your code really well before submitting it.
**
***********************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#include <windows.h>
#endif

#include "reb-host.h"		// standard host include files
#include "host-table.inc"

#ifdef CUSTOM_STARTUP
#include "host-init.h"
#endif

/**********************************************************************/

REBARGS Main_Args;

const REBYTE halt_str[] = "[escape]";
const REBYTE prompt_str[] = ">> ";
const REBYTE result_str[] = "== ";
const REBYTE why_str[] = "** Note: use WHY? for more error information\n\n";

#ifdef TO_WINDOWS
HINSTANCE App_Instance = 0;
#endif

#ifndef REB_CORE
extern void Init_Windows(void);
extern void OS_Init_Graphics(void);
extern void OS_Destroy_Graphics(void);
#endif

extern void Init_Core_Ext(void);
extern void Shutdown_Core_Ext(void);

//#define TEST_EXTENSIONS
#ifdef TEST_EXTENSIONS
extern void Init_Ext_Test(void);	// see: host-ext-test.c
#endif

// Host bare-bones stdio functs:
extern void Open_StdIO(void);
extern void Close_StdIO(void);
extern void Put_Str(const REBYTE *buf);
extern REBYTE *Get_Str();


/* coverity[+kill] */
void Host_Crash(const char *reason) {
	OS_Crash(cb_cast("REBOL Host Failure"), cb_cast(reason));
}


REBOOL Host_Start_Exiting(int *exit_status, int argc, REBCHR **argv) {
	REBYTE vers[8];
	REBINT startup_rc;
	REBYTE *embedded_script = NULL;
	REBI64 embedded_size = 0;

	Host_Lib = &Host_Lib_Init;

	embedded_script = OS_Read_Embedded(&embedded_size);

	// !!! Note we may have to free Main_Args.home_dir below after this
	Parse_Args(argc, argv, &Main_Args);

	vers[0] = 5; // len
	RL_Version(&vers[0]);

	// Must be done before an console I/O can occur. Does not use reb-lib,
	// so this device should open even if there are other problems.
	Open_StdIO();  // also sets up interrupt handler

	// Initialize the REBOL library (reb-lib):
	if (!CHECK_STRUCT_ALIGN) Host_Crash("Incompatible struct alignment");
	if (!Host_Lib) Host_Crash("Missing host lib");
	// !!! Second part will become vers[2] < RL_REV on release!!!
	if (vers[1] != RL_VER || vers[2] != RL_REV) Host_Crash("Incompatible reb-lib DLL");

	startup_rc = RL_Init(&Main_Args, Host_Lib);

	// !!! Not a good abstraction layer here, but Parse_Args may have put
	// an OS_ALLOC'd string into home_dir, via OS_Get_Current_Dir
	if (Main_Args.home_dir) OS_FREE(Main_Args.home_dir);

	if (startup_rc == 1) Host_Crash("Host-lib wrong size");
	if (startup_rc == 2) Host_Crash("Host-lib wrong version/checksum");

	//Initialize core extension commands
	Init_Core_Ext();

#ifdef TEST_EXTENSIONS
	Init_Ext_Test();
#endif

#ifdef TO_WINDOWS
	// no console, we must be the child process
	if (GetStdHandle(STD_OUTPUT_HANDLE) == 0)
	{
		App_Instance = GetModuleHandle(NULL);
	}
#ifdef REB_CORE
	else //use always the console for R3/core
	{
		// GetWindowsLongPtr support 32 & 64 bit windows
		App_Instance = (HINSTANCE)GetWindowLongPtr(GetConsoleWindow(), GWLP_HINSTANCE);
	}
#else
	//followinng R3/view code behaviors when compiled as:
	//-"console app" mode: stdio redirection works but blinking console window during start
	//-"GUI app" mode stdio redirection doesn't work properly, no blinking console window during start
	else if (argc > 1) // we have command line args
	{
		// GetWindowsLongPtr support 32 & 64 bit windows
		App_Instance = (HINSTANCE)GetWindowLongPtr(GetConsoleWindow(), GWLP_HINSTANCE);
	}
	else // no command line args but a console - launch child process so GUI is initialized and exit
	{
		DWORD dwCreationFlags = CREATE_DEFAULT_ERROR_MODE | DETACHED_PROCESS;
		STARTUPINFO startinfo;
		PROCESS_INFORMATION procinfo;
		ZeroMemory(&startinfo, sizeof(startinfo));
		startinfo.cb = sizeof(startinfo);
		if (!CreateProcess(NULL, argv[0], NULL, NULL, FALSE, dwCreationFlags, NULL, NULL, &startinfo, &procinfo))
			MessageBox(0, L"CreateProcess() failed :(", L"", 0);
		exit(0);
	}
#endif //REB_CORE
#endif //TO_WINDOWS

	// Common code for console & GUI version
#ifndef REB_CORE
	Init_Windows();
	OS_Init_Graphics();
#endif // REB_CORE

	// Call sys/start function. If a compressed script is provided, it will be
	// decompressed, stored in system/options/boot-host, loaded, and evaluated.
	// Returns: 0: ok, -1: error, 1: bad data.
#ifdef CUSTOM_STARTUP
	// For custom startup, you can provide compressed script code here:
	startup_rc = RL_Start(
		&Reb_Init_Code[0], REB_INIT_SIZE,
		embedded_script, embedded_size, 0
	);
#else
	startup_rc = RL_Start(0, 0, embedded_script, embedded_size, 0);
#endif

#if !defined(ENCAP)
	// !!! What should an encapped executable do with a --do?  Here we just
	// ignore it, as the assumption is that it is a packaged system that
	// doesn't necessarily want to present itself as an arbitrary interpreter

	// Previously this command line option was handled by the Rebol Core
	// itself, in Mezzanine initialization.  However, Ren/C is catering to
	// needs of other kinds of clients.  So rather than having those clients
	// figure out how to send Rebol a "--do" option in a "command line
	// arguments buffer", it is turned the other way so that if something
	// does have a command line it needs to call APIs to run them.  This
	// "pulled out" piece of command line processing uses the RL_Api still,
	// with RL_Do_String (more options will be available with Ren/C proper)

	// !!! NOTE: Encapping needs to be thought of similarly; it is not a
	// Ren/C feature, rather a feature that some client (e.g. a console
	// client named "Rebol") would implement.

	// !!! The command line processing tells us if we have just '--do' with
	// nothing afterward by setting do_arg to NULL.  When all the command
	// line processing is taken out of Ren/C's concern that kind of decision
	// can be revisited.  In the meantime, we test for NULL.

	if (startup_rc >= 0 && (Main_Args.options & RO_DO) && Main_Args.do_arg) {
		RXIARG result;
		REBYTE *do_arg_utf8;
		REBCNT len_uni;
		REBCNT len_predicted;
		REBCNT len_encoded;
		int do_result;

		// On Windows, do_arg is a REBCHR*.  We need to get it into UTF8.
		// !!! Better helpers needed than this; Ren/C can call host's OS_ALLOC
		// so this should be more seamless.
	#ifdef TO_WINDOWS
		len_uni = wcslen(Main_Args.do_arg);
		len_predicted = RL_Length_As_UTF8(
			Main_Args.do_arg, len_uni, TRUE, FALSE
		);
		do_arg_utf8 = OS_ALLOC_ARRAY(REBYTE, len_predicted + 1);
		len_encoded = RL_Encode_UTF8(
			do_arg_utf8,
			len_predicted + 1,
			Main_Args.do_arg,
			&len_uni,
			TRUE,
			FALSE
		);

		// Sanity check; we shouldn't get a different answer.
		assert(len_predicted == len_encoded);

		// Encoding doesn't NULL-terminate on its own.
		do_arg_utf8[len_encoded] = '\0';
	#else
		do_arg_utf8 = b_cast(Main_Args.do_arg);
	#endif

		do_result = RL_Do_String(exit_status, do_arg_utf8, 0, NULL);

	#ifdef TO_WINDOWS
		OS_FREE(do_arg_utf8);
	#endif

		if (do_result == -1) {
			// The user canceled via a HALT signal, e.g. Ctrl-C.  For now we
			// print a halt message and exit with a made-up error code.

			Put_Str(halt_str);

			*exit_status = 100; // !!! Arbitrary number, should be configurable
			return TRUE;
		}
		else if (do_result == -2) {
			// There was a purposeful QUIT or EXIT, exit_status has any /WITH
			// translated into an integer
			return TRUE;
		}
		else if (do_result < -2) {
			// There was an error, so print it out.
			RL_Print_TOS(FALSE, NULL);
			RL_Drop_TOS();

			// We invent a status and exit, but the response to an error
			// should be more flexible.  See #2215.

			*exit_status = 101; // !!! Arbitrary number, should be configurable
			return TRUE;
		}
		else {
			assert(do_result >= 0);

			// Command completed successfully, we don't print anything.
			RL_Drop_TOS();

			// We quit vs. dropping to interpreter by default, but it would be
			// good to have a more flexible response here too.  See #2215.
			*exit_status = 0;
			return TRUE;
		}
	}
#endif //!ENCAP

	// If we get here we didn't have something happen that translates to
	// needing us to definitely exit.  So `exit_status` is uninitialized.
	return FALSE;
}


void Host_Repl(int *exit_status) {
	REBOOL why_alert = TRUE;

	#define MAX_CONT_LEVEL 80
	REBYTE cont_str[] = "    ";
	int cont_level = 0;
	REBYTE cont_stack[MAX_CONT_LEVEL] = {0};

	int input_max = 32768;
	int input_len = 0;
	REBYTE *input = OS_ALLOC_ARRAY(REBYTE, input_max);

	REBYTE *line;
	int line_len;

	REBYTE *utf8byte;
	BOOL inside_short_str = FALSE;
	int long_str_level = 0;

	while (TRUE) {
		int do_result;

		if (cont_level > 0) {
			int level;

			cont_str[0] = cont_stack[cont_level - 1];
			Put_Str(cont_str);

			cont_str[0] = ' ';
			for (level = 1; level < cont_level; level++) {
				Put_Str(cont_str);
			}
		} else {
			Put_Str(prompt_str);
		}

		line = Get_Str();

		if (!line) {
			// !!! "end of stream"...is this a normal exit result or
			// should we be returning some error here?  0 status for now
			*exit_status = 0;
			goto cleanup_and_return;
		}

		line_len = 0;
		for (utf8byte = line; *utf8byte; utf8byte++) {
			line_len++;
			switch (*utf8byte) {
				case '"':
					if (long_str_level == 0) {
						inside_short_str = !inside_short_str;
					}
					break;
				case '[':
				case '(':
					if (!inside_short_str && long_str_level == 0) {
						cont_stack[cont_level++] = *utf8byte;
						if (cont_level >= MAX_CONT_LEVEL) {
							OS_FREE(input);
							Host_Crash("Maximum console continuation level exceeded!");
						}
					}
					break;
				case ']':
				case ')':
					if (!inside_short_str && long_str_level == 0) {
						if (cont_level > 0) {
							cont_stack[--cont_level] = 0;
						}
					}
					break;
				case '{':
					if (!inside_short_str) {
						cont_stack[cont_level++] = *utf8byte;
						if (cont_level >= MAX_CONT_LEVEL) {
							OS_FREE(input);
							Host_Crash("Maximum console continuation level exceeded!");
						}
						long_str_level++;
					}
					break;
				case '}':
					if (!inside_short_str) {
						if (cont_level > 0) {
							cont_stack[--cont_level] = 0;
						}
						if (long_str_level > 0) {
							long_str_level--;
						}
					}
					break;
			}
		}
		inside_short_str = FALSE;

		if (input_len + line_len > input_max) {
			REBYTE *tmp = OS_ALLOC_ARRAY(REBYTE, 2 * input_max);
			if (!tmp) {
				OS_FREE(input);
				Host_Crash("Growing console input buffer failed!");
			}
			memcpy(tmp, input, input_len);
			OS_FREE(input);
			input = tmp;
			input_max *= 2;
		}

		memcpy(&input[input_len], line, line_len);
		input_len += line_len;
		input[input_len] = 0;

		OS_FREE(line);

		if (cont_level > 0)
			continue;

		input_len = 0;
		cont_level = 0;

		do_result = RL_Do_String(exit_status, input, 0, 0);

		if (do_result == -1) {
			// !!! The "Halt" status is communicated via -1, but
			// is not an actual valid "error value".  It cannot be
			// created by user code, and the fact that it is done
			// via the error mechanism is an "implementation detail".
			// Hence nothing is pushed to the stack.

			Put_Str(halt_str);
		}
		else if (do_result == -2) {
			// Command issued a purposeful QUIT or EXIT, exit_status
			// contains status.  Assume nothing was pushed on stack
			goto cleanup_and_return;
		}
		else if (do_result < -2) {
			// Error occurred, print it without molding (formed)
			RL_Print_TOS(FALSE, NULL);
			RL_Drop_TOS();

			// Tell them about why on the first error only
			if (why_alert) {
				Put_Str(why_str);
				why_alert = FALSE;
			}
		}
		else {
			assert(do_result >= 0);

			// There was no error, and the value is on the top of
			// stack.  If the value on top of stack is an unset
			// then nothing will be printed.

			RL_Print_TOS(TRUE, result_str);
			RL_Drop_TOS();
		}
	}

cleanup_and_return:
	OS_FREE(input);
	return;
}


void Host_Quit() {
	OS_Quit_Devices(0);
#ifndef REB_CORE
	OS_Destroy_Graphics();
#endif
	Shutdown_Core_Ext();
}


/***********************************************************************
**
**  MAIN ENTRY POINT
**
**	Win32 args:
**		inst:  current instance of the application (app handle)
**		prior: always NULL (use a mutex for single inst of app)
**		cmd:   command line string (or use GetCommandLine)
**	    show:  how app window is to be shown (e.g. maximize, minimize, etc.)
**
**	Win32 return:
**		If the function succeeds, terminating when it receives a WM_QUIT
**		message, it should return the exit value contained in that
**		message's wParam parameter. If the function terminates before
**		entering the message loop, it should return zero.
**
**  Posix args: as you would expect in C.
**  Posix return: ditto.
**
*/
/***********************************************************************/

// Using a main entry point for a console program (as opposed to WinMain)
// so that we can connect to the console.  See the StackOverflow question
// "Can one executable be both a console and a GUI application":
//
//     http://stackoverflow.com/questions/493536/
//
// int WINAPI WinMain(HINSTANCE inst, HINSTANCE prior, LPSTR cmd, int show)

int main(int argc, char **argv_ansi)
{
	int exit_status;

	REBINT startup_rc;
	REBCHR **argv;

#ifdef TO_WINDOWS
	// Were we using WinMain we'd be getting our arguments in Unicode, but
	// since we're using an ordinary main() we do not.  However, this call
	// lets us slip out and pick up the arguments in Unicode form.
	argv = CommandLineToArgvW(GetCommandLineW(), &argc);
#else
	// Assume no wide character support, and just take the ANSI C args
	argv = argv_ansi;
#endif

	if (Host_Start_Exiting(&exit_status, argc, argv))
		goto cleanup_and_exit; // exit status is set...

#if !defined(ENCAP)
	// Console line input loop (just an example, can be improved):
	if (
		!(Main_Args.options & RO_CGI)
		&& (
			!Main_Args.script               // no script was provided
			|| Main_Args.options & RO_HALT  // --halt option
		)
	) {
		Host_Repl(&exit_status);
	}
	else
		exit_status = 0; // "success"
#else
	exit_status = 0; // "success"
#endif

cleanup_and_exit:
	Host_Quit();

	Close_StdIO();

	// No need to do a "clean" shutdown, as we are about to exit the process
	// (Note: The debug build runs through the clean shutdown anyway!)
	RL_Shutdown(FALSE);

	return exit_status;
}

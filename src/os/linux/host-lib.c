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
**  Title: Linux OS API function library called by REBOL interpreter
**  Author: Carl Sassenrath, Richard Smolak
**  Purpose:
**      This module provides the functions that REBOL calls
**      to interface to the native (host) operating system.
**      REBOL accesses these functions through the structure
**      defined in host-lib.h (auto-generated, do not modify).
**
**  Special note:
**      This module is parsed for function declarations used to
**      build prototypes, tables, and other definitions. To change
**      function arguments requires a rebuild of the REBOL library.
**
************************************************************************
**
**  NOTE to PROGRAMMERS:
**
**    1. Keep code clear and simple.
**    2. Document unusual code, reasoning, or gotchas.
**    3. Use same style for code, vars, indent(4), comments, etc.
**    4. Keep in mind Linux, OS X, BSD, big/little endian CPUs.
**    5. Test everything, then test it again.
**
***********************************************************************/

/* WARNING:
**     The function declarations here cannot be modified without
**     also modifying those found in the other OS host-lib files!
**     Do not even modify the argument names.
*/

#define _GNU_SOURCE             /* See feature_test_macros(7) */
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

#ifndef timeval // for older systems
#include <sys/time.h>
#endif

#include "reb-host.h"

#ifndef NO_DL_LIB
#include <dlfcn.h>
#endif

#include <elf.h>

#ifndef REB_CORE
REBSER* Gob_To_Image(REBGOB *gob);
#endif

// Semaphore lock to sync sub-task launch:
static void *Task_Ready;

#ifndef PATH_MAX
#define PATH_MAX 4096  // generally lacking in Posix
#endif

const char ** iso639_find_entry_by_2_code(const char* code);
const char ** iso3166_find_entry_by_2_code(const char* code);

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
*/	static int Get_Timezone(struct tm *local_tm)
/*
**		Get the time zone in minutes from GMT.
**		NOT consistently supported in Posix OSes!
**		We have to use a few different methods.
**
***********************************************************************/
{
#ifdef HAS_SMART_TIMEZONE
	time_t rightnow;
	time(&rightnow);
	return (int)difftime(mktime(localtime(&rightnow)), mktime(gmtime(&rightnow))) / 60;
#else
	struct tm tm2;
	time_t rightnow;
	time(&rightnow);
	tm2 = *localtime(&rightnow);
	tm2.tm_isdst=0;
	return (int)difftime(mktime(&tm2), mktime(gmtime(&rightnow))) / 60;
#endif
//	 return local_tm->tm_gmtoff / 60;  // makes the most sense, but no longer used
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
**	OS Library Functions
**
***********************************************************************/

/* Keep in sync with n-io.c */
#define OS_ENA	 -1
#define OS_EINVAL -2
#define OS_EPERM -3
#define OS_ESRCH -4

/***********************************************************************
**
*/	REBINT OS_Get_PID()
/*
**		Return the current process ID
**
***********************************************************************/
{
	return getpid();
}

/***********************************************************************
**
*/	REBINT OS_Get_UID()
/*
**		Return the real user ID
**
***********************************************************************/
{
	return getuid();
}

/***********************************************************************
**
*/	REBINT OS_Set_UID(REBINT uid)
/*
**		Set the user ID, see setuid manual for its semantics
**
***********************************************************************/
{
	if (setuid(uid) < 0) {
		switch (errno) {
			case EINVAL:
				return OS_EINVAL;
			case EPERM:
				return OS_EPERM;
			default:
				return -errno;
		}
	} else {
		return 0;
	}
}

/***********************************************************************
**
*/	REBINT OS_Get_GID()
/*
**		Return the real group ID
**
***********************************************************************/
{
	return getgid();
}

/***********************************************************************
**
*/	REBINT OS_Set_GID(REBINT gid)
/*
**		Set the group ID, see setgid manual for its semantics
**
***********************************************************************/
{
	if (setgid(gid) < 0) {
		switch (errno) {
			case EINVAL:
				return OS_EINVAL;
			case EPERM:
				return OS_EPERM;
			default:
				return -errno;
		}
	} else {
		return 0;
	}
}

/***********************************************************************
**
*/	REBINT OS_Get_EUID()
/*
**		Return the effective user ID
**
***********************************************************************/
{
	return geteuid();
}

/***********************************************************************
**
*/	REBINT OS_Set_EUID(REBINT uid)
/*
**		Set the effective user ID
**
***********************************************************************/
{
	if (seteuid(uid) < 0) {
		switch (errno) {
			case EINVAL:
				return OS_EINVAL;
			case EPERM:
				return OS_EPERM;
			default:
				return -errno;
		}
	} else {
		return 0;
	}
}

/***********************************************************************
**
*/	REBINT OS_Get_EGID()
/*
**		Return the effective group ID
**
***********************************************************************/
{
	return getegid();
}

/***********************************************************************
**
*/	REBINT OS_Set_EGID(REBINT gid)
/*
**		Set the effective group ID
**
***********************************************************************/
{
	if (setegid(gid) < 0) {
		switch (errno) {
			case EINVAL:
				return OS_EINVAL;
			case EPERM:
				return OS_EPERM;
			default:
				return -errno;
		}
	} else {
		return 0;
	}
}

/***********************************************************************
**
*/	REBINT OS_Send_Signal(REBINT pid, REBINT signal)
/*
**		Send signal to a process
**
***********************************************************************/
{
	if (kill(pid, signal) < 0) {
		switch (errno) {
			case EINVAL:
				return OS_EINVAL;
			case EPERM:
				return OS_EPERM;
			case ESRCH:
				return OS_ESRCH;
			default:
				return -errno;
		}
	} else {
		return 0;
	}
}

/***********************************************************************
**
*/	REBINT OS_Kill(REBINT pid)
/*
**		Try to kill the process
**
***********************************************************************/
{
	return OS_Send_Signal(pid, SIGTERM);
}

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
*/	void *OS_Alloc_Mem(size_t size)
/*
**		Allocate memory of given size.
**
**		This is necessary because some environments may use their
**		own specific memory allocation (e.g. private heaps).
**
***********************************************************************/
{
	return malloc(size);
}


/***********************************************************************
**
*/	void OS_Free_Mem(void *mem)
/*
**		Free memory allocated in this OS environment. (See OS_Alloc_Mem)
**
***********************************************************************/
{
	free(mem);
}


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
	char *msg = NULL;
	if (!errnum) errnum = errno;
	msg = strerror_r(errnum, str, len);
	if (msg != NULL && msg != (char*)str) {
		strncpy(str, msg, len);
		str[len - 1] = '\0'; /* to be safe */
	}
	return msg;
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
*/	REBCHR *OS_Get_Locale(int what)
/*
**		Used to obtain locale information from the system.
**		The returned value must be freed with OS_FREE_MEM.
**
***********************************************************************/
{
	if (what > 3 || what < 0) {
		return NULL;
	}
	unsigned int i = 0;
	unsigned int j = 0;
	char *lang = NULL;
	char *territory = NULL;
	const char *lang_env = getenv("LANG"); /* something like: lang_territory.codeset */
	if (lang_env == NULL){
		return NULL;
	}
	for(i = 0; i < strlen(lang_env); i ++){
		if (lang_env[i] == '_'){
			if (lang != NULL) { /* duplicate "_" */
				goto error;
			}
			lang = OS_ALLOC_ARRAY(char, i + 1);
			if (lang == NULL) goto error;
			strncpy(lang, lang_env, i);
			lang[i] = '\0';
			j = i;
		} else if (lang_env[i] == '.'){
			if (i == j) goto error;
			territory = OS_ALLOC_ARRAY(char, i - j);
			if (territory == NULL) goto error;
			strncpy(territory, lang_env + j + 1, i - j - 1);
			territory[i - j - 1] = '\0';
			break;
		}
	}

	if (lang == NULL || territory == NULL) goto error;

	const char ** iso639_entry = iso639_find_entry_by_2_code(lang);
	OS_FREE(lang);
	lang = NULL;
	if (iso639_entry == NULL) goto error;

	const char ** iso3166_entry = iso3166_find_entry_by_2_code(territory);
	OS_FREE(territory);
	territory = NULL;

	const char *ret[] = {
		iso639_entry[3], iso639_entry[3], iso3166_entry[1], iso3166_entry[1]
	};
	return strdup(ret[what]);

error:
	if (lang != NULL) {
		OS_FREE(lang);
	}
	if (territory != NULL) {
		OS_FREE(territory);
	}
	return NULL;
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
	extern char **environ;
	int n, len = 0;
	char *str, *cp;

	// compute total size:
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

	time = ((i64)tv.tv_sec * 1000000) + tv.tv_usec;

	if (base == 0) return time;

	return time - base;
}


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
*/	void OS_File_Time(REBREQ *file, REBOL_DAT *dat)
/*
**		Convert file.time to REBOL date/time format.
**		Time zone is UTC.
**
***********************************************************************/
{
	if (sizeof(time_t) > sizeof(file->file.time.l)) {
		REBI64 t = file->file.time.l;
		t |= ((REBI64)file->file.time.h) << 32;
		Convert_Date((time_t*)&t, dat, 0);
	} else {
		Convert_Date((time_t *)&(file->file.time.l), dat, 0);
	}
}


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
*/	void *OS_Find_Function(void *dll, const char *funcname)
/*
**		Get a DLL function address from its string name.
**
***********************************************************************/
{
#ifndef NO_DL_LIB
	void *fp = dlsym(dll, funcname);
	return fp;
#else
	return 0;
#endif
}


/***********************************************************************
**
*/	REBINT OS_Create_Thread(CFUNC init, void *arg, REBCNT stack_size)
/*
**		Creates a new thread for a REBOL task datatype.
**
**	NOTE:
**		For this to work, the multithreaded library option is
**		needed in the C/C++ code generation settings.
**
**		The Task_Ready stops return until the new task has been
**		initialized (to avoid unknown new thread state).
**
***********************************************************************/
{
	REBINT thread;
/*
	Task_Ready = CreateEvent(NULL, TRUE, FALSE, "REBOL_Task_Launch");
	if (!Task_Ready) return -1;

	thread = _beginthread(init, stack_size, arg);

	if (thread) WaitForSingleObject(Task_Ready, 2000);
	CloseHandle(Task_Ready);
*/
	return 1;
}


/***********************************************************************
**
*/	void OS_Delete_Thread(void)
/*
**		Can be called by a REBOL task to terminate its thread.
**
***********************************************************************/
{
	//_endthread();
}


/***********************************************************************
**
*/	void OS_Task_Ready(REBINT tid)
/*
**		Used for new task startup to resume the thread that
**		launched the new task.
**
***********************************************************************/
{
	//SetEvent(Task_Ready);
}

/***********************************************************************
**
*/	int OS_Create_Process(REBCHR *call, int argc, const char* argv[], u32 flags, u64 *pid, int *exit_code, u32 input_type, void *input, u32 input_len, u32 output_type, void **output, u32 *output_len, u32 err_type, void **err, u32 *err_len)
/*
 * flags:
 * 		1: wait, is implied when I/O redirection is enabled
 * 		2: console
 * 		4: shell
 * 		8: info
 * 		16: show
 * input_type/output_type/err_type:
 * 		0: none
 * 		1: string
 * 		2: file
 *
**		Return -1 on error, otherwise the process return code.
**
***********************************************************************/
{
#define INHERIT_TYPE 0
#define NONE_TYPE 1
#define STRING_TYPE 2
#define FILE_TYPE 3
#define BINARY_TYPE 4

#define FLAG_WAIT 1
#define FLAG_CONSOLE 2
#define FLAG_SHELL 4
#define FLAG_INFO 8

#define R 0
#define W 1
	unsigned char flag_wait = FALSE;
	unsigned char flag_console = FALSE;
	unsigned char flag_shell = FALSE;
	unsigned char flag_info = FALSE;
	int stdin_pipe[] = {-1, -1};
	int stdout_pipe[] = {-1, -1};
	int stderr_pipe[] = {-1, -1};
	int info_pipe[] = {-1, -1};
	int status = 0;
	int ret = 0;
	char *info = NULL;
	off_t info_size = 0;
	u32 info_len = 0;
	pid_t fpid = 0;

	// We want to be able to compile with all warnings as errors, and
	// we'd like to use -Wcast-qual if possible.  This is currently
	// the only barrier in the codebase...so we tunnel under the cast.
	char * const *argv_hack;

	if (flags & FLAG_WAIT) flag_wait = TRUE;
	if (flags & FLAG_CONSOLE) flag_console = TRUE;
	if (flags & FLAG_SHELL) flag_shell = TRUE;
	if (flags & FLAG_INFO) flag_info = TRUE;

	// suppress unused warnings but keep flags for future use
	(void)flag_info;
	(void)flag_console;

	if (input_type == STRING_TYPE
		|| input_type == BINARY_TYPE) {
		if (pipe2(stdin_pipe, O_CLOEXEC | O_NONBLOCK) < 0) {
			goto stdin_pipe_err;
		}
	}
	if (output_type == STRING_TYPE
		|| output_type == BINARY_TYPE) {
		if (pipe2(stdout_pipe, O_CLOEXEC | O_NONBLOCK) < 0) {
			goto stdout_pipe_err;
		}
	}
	if (err_type == STRING_TYPE
		|| err_type == BINARY_TYPE) {
		if (pipe2(stderr_pipe, O_CLOEXEC | O_NONBLOCK) < 0) {
			goto stderr_pipe_err;
		}
	}

	if (pipe2(info_pipe, O_CLOEXEC | O_NONBLOCK) < 0) {
		goto info_pipe_err;
	}

	fpid = fork();
	if (fpid == 0) {
		/* child */
		if (input_type == STRING_TYPE
			|| input_type == BINARY_TYPE) {
			close(stdin_pipe[W]);
			if (dup2(stdin_pipe[R], STDIN_FILENO) < 0) {
				goto child_error;
			}
			close(stdin_pipe[R]);
		} else if (input_type == FILE_TYPE) {
			int fd = open(input, O_RDONLY);
			if (fd < 0) {
				goto child_error;
			}
			if (dup2(fd, STDIN_FILENO) < 0) {
				goto child_error;
			}
			close(fd);
		} else if (input_type == NONE_TYPE) {
			int fd = open("/dev/null", O_RDONLY);
			if (fd < 0) {
				goto child_error;
			}
			if (dup2(fd, STDIN_FILENO) < 0) {
				goto child_error;
			}
			close(fd);
		} else { /* inherit stdin from the parent */
		}

		if (output_type == STRING_TYPE
			|| output_type == BINARY_TYPE) {
			close(stdout_pipe[R]);
			if (dup2(stdout_pipe[W], STDOUT_FILENO) < 0) {
				goto child_error;
			}
			close(stdout_pipe[W]);
		} else if (output_type == FILE_TYPE) {
			int fd = open(*output, O_CREAT|O_WRONLY, 0666);
			if (fd < 0) {
				goto child_error;
			}
			if (dup2(fd, STDOUT_FILENO) < 0) {
				goto child_error;
			}
			close(fd);
		} else if (output_type == NONE_TYPE) {
			int fd = open("/dev/null", O_WRONLY);
			if (fd < 0) {
				goto child_error;
			}
			if (dup2(fd, STDOUT_FILENO) < 0) {
				goto child_error;
			}
			close(fd);
		} else { /* inherit stdout from the parent */
		}

		if (err_type == STRING_TYPE
			|| err_type == BINARY_TYPE) {
			close(stderr_pipe[R]);
			if (dup2(stderr_pipe[W], STDERR_FILENO) < 0) {
				goto child_error;
			}
			close(stderr_pipe[W]);
		} else if (err_type == FILE_TYPE) {
			int fd = open(*err, O_CREAT|O_WRONLY, 0666);
			if (fd < 0) {
				goto child_error;
			}
			if (dup2(fd, STDERR_FILENO) < 0) {
				goto child_error;
			}
			close(fd);
		} else if (err_type == NONE_TYPE) {
			int fd = open("/dev/null", O_WRONLY);
			if (fd < 0) {
				goto child_error;
			}
			if (dup2(fd, STDERR_FILENO) < 0) {
				goto child_error;
			}
			close(fd);
		} else { /* inherit stderr from the parent */
		}

		close(info_pipe[R]);

		//printf("flag_shell in child: %hhu\n", flag_shell);
		if (flag_shell) {
			const char* sh = NULL;
			const char ** argv_new = NULL;
			sh = getenv("SHELL");
			if (sh == NULL) {
				int err = 2; /* shell does not exist */
				write(info_pipe[W], &err, sizeof(err));
				exit(EXIT_FAILURE);
			}
			argv_new = c_cast(const char**, OS_ALLOC_ARRAY(const char*, argc + 3));
			argv_new[0] = sh;
			argv_new[1] = "-c";
			memcpy(&argv_new[2], argv, argc * sizeof(argv[0]));
			argv_new[argc + 2] = NULL;

			memcpy(&argv_hack, argv_new, sizeof(argv_hack));
			execvp(sh, argv_hack);
		} else {
			memcpy(&argv_hack, argv, sizeof(argv_hack));
			execvp(argv[0], argv_hack);
		}
child_error:
		write(info_pipe[W], &errno, sizeof(errno));
		exit(EXIT_FAILURE); /* get here only when exec fails */
	} else if (fpid > 0) {
		/* parent */
#define BUF_SIZE_CHUNK 4096
		nfds_t nfds = 0;
		struct pollfd pfds[4];
		pid_t xpid;
		unsigned int i;
		ssize_t nbytes;
		off_t input_size = 0;
		off_t output_size = 0;
		off_t err_size = 0;
		int exited = 0;

		/* initialize outputs */
		if (output_type != NONE_TYPE
			&& output_type != INHERIT_TYPE
			&& (output == NULL
				|| output_len == NULL)) {
			return -1;
		}
		if (output != NULL) *output = NULL;
		if (output_len != NULL) *output_len = 0;

		if (err_type != NONE_TYPE
			&& err_type != INHERIT_TYPE
			&& (err == NULL
				|| err_len == NULL)) {
			return -1;
		}
		if (err != NULL) *err = NULL;
		if (err_len != NULL) *err_len = 0;

		if (stdin_pipe[W] > 0) {
			//printf("stdin_pipe[W]: %d\n", stdin_pipe[W]);
			input_size = strlen(input); /* the passed in input_len is in character, not in bytes */
			input_len = 0;
			pfds[nfds++] = (struct pollfd){.fd = stdin_pipe[W], .events = POLLOUT};
			close(stdin_pipe[R]);
			stdin_pipe[R] = -1;
		}
		if (stdout_pipe[R] > 0) {
			//printf("stdout_pipe[R]: %d\n", stdout_pipe[R]);
			output_size = BUF_SIZE_CHUNK;

			// !!! Uses realloc(), can't use OS_ALLOC_ARRAY
			*output = cast(char*, malloc(output_size));
			pfds[nfds++] = (struct pollfd){.fd = stdout_pipe[R], .events = POLLIN};

			close(stdout_pipe[W]);
			stdout_pipe[W] = -1;
		}
		if (stderr_pipe[R] > 0) {
			//printf("stderr_pipe[R]: %d\n", stderr_pipe[R]);
			err_size = BUF_SIZE_CHUNK;

			// !!! Uses realloc(), can't use OS_ALLOC_ARRAY
			*err = cast(char*, malloc(err_size));
			pfds[nfds++] = (struct pollfd){.fd = stderr_pipe[R], .events = POLLIN};

			close(stderr_pipe[W]);
			stderr_pipe[W] = -1;
		}

		if (info_pipe[R] > 0) {
			pfds[nfds++] = (struct pollfd){.fd = info_pipe[R], .events = POLLIN};
			info_size = 4;
			// !!! Uses realloc(), can't use OS_ALLOC_ARRAY
			info = cast(char*, malloc(info_size));
			close(info_pipe[W]);
			info_pipe[W] = -1;
		}

		int valid_nfds = nfds;
		while (valid_nfds > 0) {
			xpid = waitpid(fpid, &status, WNOHANG);
			if (xpid == -1) {
				ret = errno;
				goto error;
			}

			if (xpid == fpid) {
				/* try one more time to read any remainding output/err */
				if (stdout_pipe[R] > 0) {
					nbytes = read(stdout_pipe[R], *output + *output_len, output_size - *output_len);
					if (nbytes > 0) {
						*output_len += nbytes;
					}
				}
				if (stderr_pipe[R] > 0) {
					nbytes = read(stderr_pipe[R], *err + *err_len, err_size - *err_len);
					if (nbytes > 0) {
						*err_len += nbytes;
					}
				}
				if (info_pipe[R] > 0) {
					nbytes = read(info_pipe[R], info + info_len, info_size - info_len);
					if (nbytes > 0) {
						info_len += nbytes;
					}
				}

				break;
			}

			/*
			for (i = 0; i < nfds; ++i) {
				printf(" %d", pfds[i].fd);
			}
			printf(" / %d\n", nfds);
			*/
			if (poll(pfds, nfds, -1) < 0) {
				ret = errno;
				goto kill;
			}

			for (i = 0; i < nfds && valid_nfds > 0; ++i) {
				//printf("check: %d [%d/%d]\n", pfds[i].fd, i, nfds);
				if (pfds[i].revents & POLLERR) {
					//printf("POLLERR: %d [%d/%d]\n", pfds[i].fd, i, nfds);
					close(pfds[i].fd);
					pfds[i].fd = -1;
					valid_nfds --;
				} else if (pfds[i].revents & POLLOUT) {
					//printf("POLLOUT: %d [%d/%d]\n", pfds[i].fd, i, nfds);
					nbytes = write(pfds[i].fd, input, input_size - input_len);
					if (nbytes <= 0) {
						ret = errno;
						goto kill;
					}
					//printf("POLLOUT: %d bytes\n", nbytes);
					input_len += nbytes;
					if (input_len >= input_size) {
						close(pfds[i].fd);
						pfds[i].fd = -1;
						valid_nfds --;
					}
				} else if (pfds[i].revents & POLLIN) {
					//printf("POLLIN: %d [%d/%d]\n", pfds[i].fd, i, nfds);
					char **buffer = NULL;
					u32 *offset;
					ssize_t to_read = 0;
					size_t size;
					if (pfds[i].fd == stdout_pipe[R]) {
						buffer = (char**)output;
						offset = output_len;
						size = output_size;
					} else if (pfds[i].fd == stderr_pipe[R]) {
						buffer = (char**)err;
						offset = err_len;
						size = err_size;
					} else { /* info pipe */
						buffer = &info;
						offset = &info_len;
						size = info_size;
					}
					do {
						to_read = size - *offset;
						//printf("to read %d bytes\n", to_read);
						nbytes = read(pfds[i].fd, *buffer + *offset, to_read);
						if (nbytes < 0) {
							break;
						}
						if (nbytes == 0) {
							/* closed */
							//printf("the other end closed\n");
							close(pfds[i].fd);
							pfds[i].fd = -1;
							valid_nfds --;
							break;
						}
						//printf("POLLIN: %d bytes\n", nbytes);
						*offset += nbytes;
						if (*offset >= size) {
							size += BUF_SIZE_CHUNK;
							*buffer = cast(char *,
								realloc(*buffer, size * sizeof((*buffer)[0]))
							);
							if (*buffer == NULL) goto kill;
						}
					} while (nbytes == to_read);
				} else if (pfds[i].revents & POLLHUP) {
					//printf("POLLHUP: %d [%d/%d]\n", pfds[i].fd, i, nfds);
					close(pfds[i].fd);
					pfds[i].fd = -1;
					valid_nfds --;
				} else if (pfds[i].revents & POLLNVAL) {
					//printf("POLLNVAL: %d [%d/%d]\n", pfds[i].fd, i, nfds);
					ret = errno;
					goto kill;
				}
			}
		}

		if (valid_nfds == 0 && flag_wait) {
			if (waitpid(fpid, &status, 0) < 0) {
				ret = errno;
				goto error;
			}
		}

	} else {
		/* error */
		ret = errno;
		goto error;
	}

	if (info_len > 0) {
		/* exec in child process failed */
		/* set to errno for reporting */
		ret = *(int*)info;
	} else if (WIFEXITED(status)) {
		if (exit_code != NULL) *exit_code = WEXITSTATUS(status);
		if (pid != NULL) *pid = fpid;
	} else {
		goto error;
	}

	goto cleanup;
kill:
	kill(fpid, SIGKILL);
	waitpid(fpid, NULL, 0);
error:
	if (!ret) ret = -1;
cleanup:
	if (output != NULL && *output != NULL && *output_len <= 0) {
		free(*output);
	}
	if (err != NULL && *err != NULL && *err_len <= 0) {
		free(*err);
	}
	if (info != NULL) {
		free(info);
	}
	if (info_pipe[R] > 0) {
		close(info_pipe[R]);
	}
	if (info_pipe[W] > 0) {
		close(info_pipe[W]);
	}
info_pipe_err:
	if (stderr_pipe[R] > 0) {
		close(stderr_pipe[R]);
	}
	if (stderr_pipe[W] > 0) {
		close(stderr_pipe[W]);
	}
stderr_pipe_err:
	if (stdout_pipe[R] > 0) {
		close(stdout_pipe[R]);
	}
	if (stdout_pipe[W] > 0) {
		close(stdout_pipe[W]);
	}
stdout_pipe_err:
	if (stdin_pipe[R] > 0) {
		close(stdin_pipe[R]);
	}
	if (stdin_pipe[W] > 0) {
		close(stdin_pipe[W]);
	}
stdin_pipe_err:
	printf("ret: %d\n", ret);
	return ret;
}

/***********************************************************************
**
*/	int OS_Reap_Process(int pid, int *status, int flags)
/*
 * pid:
 * 		> 0, a signle process
 * 		-1, any child process
 * flags:
 * 		0: return immediately
 *
**		Return -1 on error
***********************************************************************/
{
	return waitpid(pid, status, flags == 0? WNOHANG : 0);
}

static int Try_Browser(char *browser, REBCHR *url)
{
	char const *argv[] = {browser, url, NULL};
	return OS_Create_Process(browser, 2, argv, 0,
							NULL, /* pid */
							NULL, /* exit_code */
							INHERIT_TYPE, NULL, 0, /* input_type, void *input, u32 input_len, */
							INHERIT_TYPE, NULL, NULL, /* output_type, void **output, u32 *output_len, */
							INHERIT_TYPE, NULL, NULL); /* u32 err_type, void **err, u32 *err_len */
}

/***********************************************************************
**
*/	int OS_Browse(REBCHR *url, int reserved)
/*
***********************************************************************/
{
	return (Try_Browser("xdg-open", url) && Try_Browser("x-www-browser", url));
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

/***********************************************************************
**
*/	REBSER *OS_GOB_To_Image(REBGOB *gob)
/*
**		Render a GOB into an image. Returns an image or zero if
**		it cannot be done.
**
***********************************************************************/
{
#if (defined REB_CORE)
	return 0;
#else
	return Gob_To_Image(gob);
#endif
}

/***********************************************************************
**
*/	REBOOL As_OS_Str(REBSER *series, REBCHR **string)
/*
**	If necessary, convert a string series to platform specific format.
**  (Handy for GOB/TEXT handling).
**  If the string series is empty the resulting string is set to NULL
**
**  Function returns:
**      TRUE - if the resulting string needs to be deallocated by the caller code
**      FALSE - if REBOL string is used (no dealloc needed)
**
**  Note: REBOL strings are allowed to contain nulls.
**
***********************************************************************/
{
	int n;
	void *str;
	REBCNT len;

	if ((n = RL_Get_String(series, 0, &str)) < 0) {
		// Latin1 byte string - use as is
		*string = str;
		return FALSE;
	}

	len = n;

	//empty string check
	if (len == 0) { /* shortcut */
		*string = OS_ALLOC_ARRAY(REBCHR, 1);
		*string[0] = '\0';
	} else {
		//convert to UTF8
		REBCNT utf8_len = RL_Length_As_UTF8(str, len, TRUE, FALSE);
		*string = OS_ALLOC_ARRAY(char, utf8_len + 1);
		RL_Encode_UTF8(b_cast(*string), utf8_len, str, &len, TRUE, FALSE);
		(*string)[utf8_len] = '\0';
	}
	return TRUE;
}

/***********************************************************************
**
*/	REBYTE * OS_Read_Embedded (REBI64 *script_size)
/*
***********************************************************************/
{
#ifdef __LP64__
	Elf64_Ehdr file_header;
	Elf64_Shdr *sec_headers;
#else
	Elf32_Ehdr file_header;
	Elf32_Shdr *sec_headers;
#endif

#define PAYLOAD_NAME ".EmbEddEdREbol"

	FILE *script = NULL;
	size_t nbytes = 0;
	int i = 0;
	char *ret = NULL;
	char *embedded_script = NULL;
	size_t sec_size;

	script = fopen("/proc/self/exe", "r");
	if (script == NULL) return NULL;

	nbytes = fread(&file_header, sizeof(file_header), 1, script);
	if (nbytes < 1) {
		fclose(script);
		return NULL;
	}

	sec_size = cast(size_t, file_header.e_shnum) * file_header.e_shentsize;

#ifdef __LP64__
	sec_headers = cast(Elf64_Shdr*, OS_ALLOC_ARRAY(char, sec_size));
#else
	sec_headers = cast(Elf32_Shdr*, OS_ALLOC_ARRAY(char, sec_size));
#endif

	if (sec_headers == NULL) {
		fclose(script);
		return NULL;
	}

	if (fseek(script, file_header.e_shoff, SEEK_SET) < 0) {
		OS_FREE(sec_headers);
		fclose(script);
		return NULL;
	}

	nbytes = fread(sec_headers, file_header.e_shentsize, file_header.e_shnum, script);
	if (nbytes < file_header.e_shnum) {
		ret = NULL;
		goto header_failed;
	}

	char *shstr = OS_ALLOC_ARRAY(char,
		sec_headers[file_header.e_shstrndx].sh_size
	);
	if (shstr == NULL) {
		ret = NULL;
		goto header_failed;
	}

	if (fseek(script, sec_headers[file_header.e_shstrndx].sh_offset, SEEK_SET) < 0) {
		ret = NULL;
		goto shstr_failed;
	}

	nbytes = fread(shstr, sec_headers[file_header.e_shstrndx].sh_size, 1, script);
	if (nbytes < 1) {
		ret = NULL;
		goto shstr_failed;
	}

	for (i = 0; i < file_header.e_shnum; i ++) {
		/* check the section name */
		if (!strncmp(shstr + sec_headers[i].sh_name, PAYLOAD_NAME, sizeof(PAYLOAD_NAME))) {
			*script_size = sec_headers[i].sh_size;
			break;
		}
	}

	if (i == file_header.e_shnum) {
		ret = NULL;
		goto cleanup;
	}

	/* will be free'ed by RL_Start */
	embedded_script = OS_ALLOC_ARRAY(char, sec_headers[i].sh_size);
	if (embedded_script == NULL) {
		ret = NULL;
		goto shstr_failed;
	}
	if (fseek(script, sec_headers[i].sh_offset, SEEK_SET) < 0) {
		ret = NULL;
		goto embedded_failed;
	}

	nbytes = fread(embedded_script, 1, sec_headers[i].sh_size, script);
	if (nbytes < sec_headers[i].sh_size) {
		ret = NULL;
		goto embedded_failed;
	}

	ret = embedded_script;
	goto cleanup;

embedded_failed:
	OS_FREE(embedded_script);
cleanup:
shstr_failed:
	OS_FREE(shstr);
header_failed:
	OS_FREE(sec_headers);
	fclose(script);
	return b_cast(ret);
}

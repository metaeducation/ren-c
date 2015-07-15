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
**  Title: POSIX Process API
**  Author: Carl Sassenrath, Richard Smolak, Shixin Zeng
**  Purpose:
**      This was originally the file host-lib.c, providing the entire
**		host API.  When the host routines were broken into smaller
**		pieces, it made sense that host-lib.c be kept as the largest
**		set of related routines.  That turned out to be the process
**		related routines and support for CALL.
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


// !!! These should probably be in a header somewhere if they are used to
// communicate with this OS_Create_Process API, so the caller can agree
// on the values...

#define INHERIT_TYPE 0
#define NONE_TYPE 1
#define STRING_TYPE 2
#define FILE_TYPE 3
#define BINARY_TYPE 4

#define FLAG_WAIT 1
#define FLAG_CONSOLE 2
#define FLAG_SHELL 4
#define FLAG_INFO 8


/***********************************************************************
**
*/	int OS_Create_Process(const REBCHR *call, int argc, const REBCHR* argv[], u32 flags, u64 *pid, int *exit_code, u32 input_type, char *input, u32 input_len, u32 output_type, char **output, u32 *output_len, u32 err_type, char **err, u32 *err_len)
/*
**		flags:
**			1: wait, is implied when I/O redirection is enabled
**			2: console
**			4: shell
**			8: info
**			16: show
**
**		input_type/output_type/err_type:
**			0: none
**			1: string
**			2: file
**
**		Return -1 on error, otherwise the process return code.
**
**	POSIX previous simple version was just 'return system(call);'
**	This uses 'execvp' which is "POSIX.1 conforming, UNIX compatible"
**
***********************************************************************/
{
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

	const unsigned int R = 0;
	const unsigned int W = 1;

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

	// NOTE: pipe() is POSIX, but pipe2() is Linux-specific.

	if (input_type == STRING_TYPE
		|| input_type == BINARY_TYPE) {
	#ifdef TO_LINUX
		if (pipe2(stdin_pipe, O_CLOEXEC | O_NONBLOCK) < 0) {
	#else
		if (pipe(stdin_pipe) < 0) {
	#endif
			goto stdin_pipe_err;
		}
	}
	if (output_type == STRING_TYPE || output_type == BINARY_TYPE) {
	#ifdef TO_LINUX
		if (pipe2(stdout_pipe, O_CLOEXEC | O_NONBLOCK) < 0) {
	#else
		if (pipe(stdout_pipe) < 0) {
	#endif
			goto stdout_pipe_err;
		}
	}
	if (err_type == STRING_TYPE || err_type == BINARY_TYPE) {
	#ifdef TO_LINUX
		if (pipe2(stderr_pipe, O_CLOEXEC | O_NONBLOCK) < 0) {
	#else
		if (pipe(stderr_pipe) < 0) {
	#endif
			goto stderr_pipe_err;
		}
	}

#ifdef TO_LINUX
	if (pipe2(info_pipe, O_CLOEXEC | O_NONBLOCK) < 0) {
#else
	if (pipe(info_pipe) < 0) {
#endif
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
			argv_new = c_cast(
				const char**, OS_ALLOC_ARRAY(const char*, argc + 3)
			);
			argv_new[0] = sh;
			argv_new[1] = "-c";
			memcpy(&argv_new[2], argv, argc * sizeof(argv[0]));
			argv_new[argc + 2] = NULL;

			memcpy(&argv_hack, &argv_new, sizeof(argv_hack));
			execvp(sh, argv_hack);
		} else {
			memcpy(&argv_hack, &argv, sizeof(argv_hack));
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
		int valid_nfds;

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

			pfds[nfds].fd = stdin_pipe[W];
			pfds[nfds].events = POLLOUT;
			nfds++;

			close(stdin_pipe[R]);
			stdin_pipe[R] = -1;
		}
		if (stdout_pipe[R] > 0) {
			//printf("stdout_pipe[R]: %d\n", stdout_pipe[R]);
			output_size = BUF_SIZE_CHUNK;

			// !!! Uses realloc(), can't use OS_ALLOC_ARRAY
			*output = cast(char*, malloc(output_size));

			pfds[nfds].fd = stdout_pipe[R];
			pfds[nfds].events = POLLIN;
			nfds++;

			close(stdout_pipe[W]);
			stdout_pipe[W] = -1;
		}
		if (stderr_pipe[R] > 0) {
			//printf("stderr_pipe[R]: %d\n", stderr_pipe[R]);
			err_size = BUF_SIZE_CHUNK;

			// !!! Uses realloc(), can't use OS_ALLOC_ARRAY
			*err = cast(char*, malloc(err_size));

			pfds[nfds].fd = stderr_pipe[R];
			pfds[nfds].events = POLLIN;
			nfds++;

			close(stderr_pipe[W]);
			stderr_pipe[W] = -1;
		}

		if (info_pipe[R] > 0) {
			pfds[nfds].fd = info_pipe[R];
			pfds[nfds].events = POLLIN;
			nfds++;

			info_size = 4;
			// !!! Uses realloc(), can't use OS_ALLOC_ARRAY
			info = cast(char*, malloc(info_size));
			close(info_pipe[W]);
			info_pipe[W] = -1;
		}

		valid_nfds = nfds;
		while (valid_nfds > 0) {
			xpid = waitpid(fpid, &status, WNOHANG);
			if (xpid == -1) {
				ret = errno;
				goto error;
			}

			if (xpid == fpid) {
				/* try one more time to read any remainding output/err */
				if (stdout_pipe[R] > 0) {
					nbytes = read(
						stdout_pipe[R],
						*output + *output_len,
						output_size - *output_len
					);

					if (nbytes > 0) {
						*output_len += nbytes;
					}
				}

				if (stderr_pipe[R] > 0) {
					nbytes = read(
						stderr_pipe[R],
						*err + *err_len,
						err_size - *err_len
					);
					if (nbytes > 0) {
						*err_len += nbytes;
					}
				}

				if (info_pipe[R] > 0) {
					nbytes = read(
						info_pipe[R],
						info + info_len,
						info_size - info_len
					);
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
**		pid:
**			> 0, a signle process
**			-1, any child process
**
**		flags:
**			0: return immediately
**
**		Return -1 on error
***********************************************************************/
{
	return waitpid(pid, status, flags == 0? WNOHANG : 0);
}

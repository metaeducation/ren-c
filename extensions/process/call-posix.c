//
//  File: %call-posix.c
//  Summary: "Implemention of CALL native for POSIX"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 Atronix Engineering
// Copyright 2012-2019 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "reb-config.h"

#if !defined(__cplusplus) && TO_LINUX
    //
    // See feature_test_macros(7), this definition is redundant under C++
    //
    #define _GNU_SOURCE // Needed for pipe2 when #including <unistd.h>
#endif
#include <unistd.h>
#include <stdlib.h>

// The location of "environ" (environment variables inventory that you
// can walk on POSIX) can vary.  Some put it in stdlib, some put it
// in <unistd.h>.  And OS X doesn't define it in a header at all, you
// just have to declare it yourself.  :-/
//
// https://stackoverflow.com/a/31347357/211160
//
#if TO_OSX || TO_OPENBSD_X64
    extern char **environ;
#endif

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#if !defined(WIFCONTINUED) && TO_ANDROID
// old version of bionic doesn't define WIFCONTINUED
// https://android.googlesource.com/platform/bionic/+/c6043f6b27dc8961890fed12ddb5d99622204d6d%5E%21/#F0
    # define WIFCONTINUED(x) (WIFSTOPPED(x) && WSTOPSIG(x) == 0xffff)
#endif


#include "sys-core.h"

#include "tmp-mod-process.h"

#include "reb-process.h"


static inline bool Open_Pipe_Fails(int pipefd[2]) {
  #ifdef USE_PIPE2_NOT_PIPE
    //
    // NOTE: pipe() is POSIX, but pipe2() is Linux-specific.  With pipe() it
    // takes an additional call to fcntl() to request non-blocking behavior,
    // so it's a small amount more work.  However, there are other flags which
    // if aren't passed atomically at the moment of opening allow for a race
    // condition in threading if split, e.g. FD_CLOEXEC.
    //
    // (If you don't have FD_CLOEXEC set on the file descriptor, then all
    // instances of CALL will act as a /WAIT.)
    //
    // At time of writing, this is mostly academic...but the code needed to be
    // patched to work with pipe() since some older libcs do not have pipe2().
    // So the ability to target both are kept around, saving the pipe2() call
    // for later Linuxes known to have it (and O_CLOEXEC).
    //
    if (pipe2(pipefd, O_CLOEXEC))
        return true;
  #else
    if (pipe(pipefd) < 0)
        return true;
    int direction;  // READ=0, WRITE=1
    for (direction = 0; direction < 2; ++direction) {
        int oldflags = fcntl(pipefd[direction], F_GETFD);
        if (oldflags < 0)
            return true;
        if (fcntl(pipefd[direction], F_SETFD, oldflags | FD_CLOEXEC) < 0)
            return true;
    }
  #endif
    return false;
}

static inline bool Set_Nonblocking_Fails(int fd) {
    int oldflags = fcntl(fd, F_GETFL);
    if (oldflags < 0)
        return true;
    if (fcntl(fd, F_SETFL, oldflags | O_NONBLOCK) < 0)
        return true;

    return false;
}


//
//  Call_Core: C
//
// flags:
//     1: wait, is implied when I/O redirection is enabled
//     2: console
//     4: shell
//     8: info
//     16: show
//
// Return -1 on error, otherwise the process return code.
//
// POSIX previous simple version was just 'return system(call);'
// This uses 'execvp' which is "POSIX.1 conforming, UNIX compatible"
//
REB_R Call_Core(REBFRM *frame_) {
    PROCESS_INCLUDE_PARAMS_OF_CALL_INTERNAL_P;

    UNUSED(REF(console));  // !!! actually not paid attention to, why?

    // Make sure that if the output or error series are STRING! or BINARY!,
    // they are not read-only, before we try appending to them.
    //
    if (IS_TEXT(ARG(output)) or IS_BINARY(ARG(output)))
        ENSURE_MUTABLE(ARG(output));
    if (IS_TEXT(ARG(error)) or IS_BINARY(ARG(error)))
        ENSURE_MUTABLE(ARG(error));

    char *inbuf;
    size_t inbuf_size;

    if (not REF(input)) {
      null_input_buffer:
        inbuf = nullptr;
        inbuf_size = 0;
    }
    else switch (VAL_TYPE(ARG(input))) {
      case REB_LOGIC:
        goto null_input_buffer;

      case REB_TEXT: {
        inbuf_size = rebSpellInto(nullptr, 0, ARG(input));
        inbuf = rebAllocN(char, inbuf_size);
        size_t check;
        check = rebSpellInto(inbuf, inbuf_size, ARG(input));
        UNUSED(check);
        break; }

      case REB_FILE: {
        size_t size;
        inbuf = s_cast(rebBytes(  // !!! why fileNAME size passed in???
            &size,
            "file-to-local", ARG(input)
        ));
        inbuf_size = size;
        break; }

      case REB_BINARY: {
        inbuf = s_cast(rebBytes(&inbuf_size, ARG(input)));
        break; }

      default:
        panic (ARG(input));  // typechecking should not have allowed it
    }

    bool flag_wait = REF(wait) or (
        IS_TEXT(ARG(input)) or IS_BINARY(ARG(input))
        or IS_TEXT(ARG(output)) or IS_BINARY(ARG(output))
        or IS_TEXT(ARG(error)) or IS_BINARY(ARG(error))
    );  // I/O redirection implies /WAIT

    // We synthesize the argc and argv from the "command", and in the process
    // we do dynamic allocations of argc strings through the API.  These need
    // to be freed before we return.
    //
    int argc;
    char **argv;

    REBVAL *command = ARG(command);

    if (REF(shell)) {

      //=//// SHELL-BASED INVOCATION: COMMAND IS ONE BIG STRING ////////////=//

        char *shcmd;  // we'll be calling `$SHELL -c "your \"command\" here"`

        if (IS_TEXT(command)) {
            shcmd = rebSpell(command);  // already a string, just use it as is
        }
        else if (IS_BLOCK(command)) {
            //
            // There is some nuance in the translation of a BLOCK! into a
            // string for the bash shell.  For example, if you write:
            //
            //     call/shell [r3 --suppress "*"]
            //
            // You have used two WORD!s and a TEXT!.  But there are two very
            // different interpretations, as:
            //
            //     sh -c "r3 --suppress *"
            //     sh -c "r3 --suppress \"*\""
            //
            // Without the quotes in the shell command, the * will expand to
            // all the files in the current directory.  Both intents are valid,
            // and even if you wanted to force people to make the distinction
            // by storing things as TEXT! vs. WORD!, not all WORD!s are legal.
            //
            // Questions surrounding this overlaps with work on the SHELL
            // dialect (see %shell.r), so for now this just uses the same
            // code that the Windows version uses when breaking apart blocks.
            // If one of the block slots has a TEXT! with spaces in it or
            // contains quotes, it will be surrounded by quotes and have
            // quotes in it escaped.  But something like the above would
            // leave the `*` as-is.  So one would need to write:
            //
            //     call/shell [r3 --suppress {"*"}]
            //
            shcmd = rebSpell("argv-block-to-command*", command);
        }
        else
            fail (PAR(command));

        // Getting the environment variable via a usermode call helps paper
        // over weird getenv() quirks, but also gives a pointer we can free in
        // the argv block.
        //
        char *sh = rebSpell("any [get-env {SHELL}, {/bin/sh}]");
        //                                       ---^
        // !!! Convention usually says the $SHELL is set.  But the GitHub CI
        // environment is a case that does not seem to pass it through to
        // processes called in steps, e.g.
        //
        //     echo "SHELL is $SHELL"  # this shows /bin/bash
        //     ./r3 --do "print get-env {SHELL}"  # shows nothing
        //
        // Other environment variables work all right, so it seems something is
        // off about $SHELL in particular.
        //
        // But it could certainly be unset manually.  On Windows we just guess
        // at it as `cmd.exe`, so it doesn't seem that much worse to just guess
        // `sh` as a default.  This is usually symlinked to
        // bash or something roughly compatible (e.g. dash).

        argc = 3;
        argv = rebAllocN(char*, 4);
        argv[0] = sh;
        argv[1] = rebSpell("{-c}");
        argv[2] = shcmd;
        argv[3] = nullptr;
    }
    else {

      //=//// PLAIN EXECVP() INVOCATION: ARGV[] ARRAY OF ITEMS /////////////=//

        // If not using a shell invocation, POSIX execvp() wants an array of
        // pointers to individual argv[] string elements.  For convenience, if
        // the caller passes in TEXT! instead of a block, we break it up.
        //
        // Note: Windows can call with a single command line, but has the
        // reverse problem: if you pass in a BLOCK!, it has to turn that into
        // a single string.  (That code is reused in the shell case for POSIX
        // up above.)
        //
        if (IS_TEXT(command)) {
            REBVAL *parsed = rebValue("parse-command-to-argv*", command);
            Copy_Cell(command, parsed);
            rebRelease(parsed);
        }
        else if (not IS_BLOCK(command))
            fail (PAR(command));

        const REBVAL *block = ARG(command);
        argc = VAL_LEN_AT(block);
        assert(argc != 0);  // usermode layer checks this
        argv = rebAllocN(char*, (argc + 1));

        int i;
        for (i = 0; i < argc; i ++) {
            const RELVAL *param = VAL_ARRAY_AT_HEAD(block, i);
            if (not IS_TEXT(param))  // usermode layer ensures FILE! converted
                fail (PAR(command));
            argv[i] = rebSpell(SPECIFIC(param));
        }
        argv[argc] = nullptr;
    }

    int exit_code = 20;  // should be overwritten if actually returned

    // If a STRING! or BINARY! is used for the output or error, then that
    // is treated as a request to append the results of the pipe to them.
    //
    // !!! At the moment this is done by having the OS-specific routine
    // pass back a buffer it allocates and reallocates to be the size of the
    // full data, which is then appended after the operation is finished.
    // With CALL now an extension where all parts have access to the internal
    // API, it could be added directly to the binary or string as it goes.

    // These are initialized to avoid a "possibly uninitialized" warning.
    //
    char *outbuf = nullptr;
    size_t outbuf_used = 0;
    char *errbuf = nullptr;
    size_t errbuf_used = 0;

    int status = 0;
    int ret = 0;
    int non_errno_ret = 0; // "ret" above should be valid errno

    // An "info" pipe is used to send back an error code from the child
    // process back to the parent if there is a problem.  It only writes
    // an integer's worth of data in that case, but it may need a bigger
    // buffer if more interesting data needs to pass between them.
    //
    char *infobuf = nullptr;
    size_t infobuf_capacity = 0;
    size_t infobuf_used = 0;

    const unsigned int R = 0;
    const unsigned int W = 1;
    int stdin_pipe[] = {-1, -1};
    int stdout_pipe[] = {-1, -1};
    int stderr_pipe[] = {-1, -1};
    int info_pipe[] = {-1, -1};

    pid_t forked_pid = -1;

    if (IS_TEXT(ARG(input)) or IS_BINARY(ARG(input))) {
        if (Open_Pipe_Fails(stdin_pipe))
            goto stdin_pipe_err;
    }

    if (IS_TEXT(ARG(output)) or IS_BINARY(ARG(output))) {
        if (Open_Pipe_Fails(stdout_pipe))
            goto stdout_pipe_err;
    }

    if (IS_TEXT(ARG(error)) or IS_BINARY(ARG(error))) {
        if (Open_Pipe_Fails(stderr_pipe))
            goto stdout_pipe_err;
    }

    if (Open_Pipe_Fails(info_pipe))
        goto info_pipe_err;

    forked_pid = fork();  // can't declare here (gotos cross initialization)

    if (forked_pid < 0) {  // error
        ret = errno;
        goto error;
    }

    if (forked_pid == 0) {

    //=//// CHILD BRANCH OF FORK() ////////////////////////////////////////=//

        // In GDB if you want to debug the child you need to use:
        // `set follow-fork-mode child`:
        //
        // http://stackoverflow.com/questions/15126925/

        if (not REF(input)) {
          inherit_stdin_from_parent:
            NOOP;  // it's the default
        }
        else if (IS_TEXT(ARG(input)) or IS_BINARY(ARG(input))) {
            close(stdin_pipe[W]);
            if (dup2(stdin_pipe[R], STDIN_FILENO) < 0)
                goto child_error;
            close(stdin_pipe[R]);
        }
        else if (IS_FILE(ARG(input))) {
            char *local_utf8 = rebSpell("file-to-local", ARG(input));

            int fd = open(local_utf8, O_RDONLY);

            rebFree(local_utf8);

            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDIN_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else if (IS_LOGIC(ARG(input))) {
            if (VAL_LOGIC(ARG(input)))
                goto inherit_stdin_from_parent;

            int fd = open("/dev/null", O_RDONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDIN_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else
            panic(ARG(input));

        if (not REF(output)) {
          inherit_stdout_from_parent:
            NOOP;  // it's the default
        }
        else if (IS_TEXT(ARG(output)) or IS_BINARY(ARG(output))) {
            close(stdout_pipe[R]);
            if (dup2(stdout_pipe[W], STDOUT_FILENO) < 0)
                goto child_error;
            close(stdout_pipe[W]);
        }
        else if (IS_FILE(ARG(output))) {
            char *local_utf8 = rebSpell("file-to-local", ARG(output));

            int fd = open(local_utf8, O_CREAT | O_WRONLY, 0666);

            rebFree(local_utf8);

            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDOUT_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else if (IS_LOGIC(ARG(output))) {
            if (VAL_LOGIC(ARG(output)))
                goto inherit_stdout_from_parent;

            int fd = open("/dev/null", O_WRONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDOUT_FILENO) < 0)
                goto child_error;
            close(fd);
        }

        if (not REF(error)) {
          inherit_stderr_from_parent:
            NOOP;  // it's the default
        }
        else if (IS_TEXT(ARG(error)) or IS_BINARY(ARG(error))) {
            close(stderr_pipe[R]);
            if (dup2(stderr_pipe[W], STDERR_FILENO) < 0)
                goto child_error;
            close(stderr_pipe[W]);
        }
        else if (IS_FILE(ARG(error))) {
            char *local_utf8 = rebSpell("file-to-local", ARG(error));

            int fd = open(local_utf8, O_CREAT | O_WRONLY, 0666);

            rebFree(local_utf8);

            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDERR_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else if (IS_LOGIC(ARG(error))) {
            if (VAL_LOGIC(ARG(error)))
                goto inherit_stderr_from_parent;

            int fd = open("/dev/null", O_WRONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDERR_FILENO) < 0)
                goto child_error;
            close(fd);
        }

        // We hang up the *read* end of the info pipe--which the parent never
        // writes to, but only uses this detection to decide the process must
        // have at least gotten up to the point of exec()'ing.  Since the
        // exec() takes over this process fully if it works, it's the last
        // chance to have any signal in that case.
        //
        // !!! Given that waiting on this signal alone would miss any errors
        // in the exec itself, it's not clear why lack of use of a /WAIT would
        // delay to detect this close (vs. return as soon as possible).  This
        // should likely be rethought in a PORT!-based redesign of CALL.
        //
        close(info_pipe[R]);

    //=//// ASK EXECVP() TO RUN, REPLACING THE CURRENT PROCESS /////////////=//

        execvp(argv[0], argv);

    //=//// FORK()'D BRANCH SHOULD ONLY GET HERE IF THERE'S AN ERROR ///////=//

        // Note: execvp() will take over the process and not return, unless
        // there was a problem in the execution.  So you shouldn't be able
        // to get here *unless* there was an error, which will be in errno.

      child_error: ;  // semicolon necessary, next statement is declaration

        // The original implementation of this code would write errno to the
        // info pipe.  However, errno may be volatile (and it is on Android).
        // write() does not accept volatile pointers, so copy it to a
        // temporary value first.
        //
        int nonvolatile_errno = errno;

        if (write(info_pipe[W], &nonvolatile_errno, sizeof(int)) == -1) {
            //
            // Nothing we can do, but need to stop compiler warning
            // (cast to void is insufficient for warn_unused_result)
            //
            assert(false);
        }
        exit(EXIT_FAILURE);  // get here only when exec fails
    }
    else {

    //=//// PARENT BRANCH OF FORK() ///////////////////////////////////////=//

        // The parent branch is the Rebol making the CALL.  It may or may not
        // /WAIT on the child fork branch, based on /WAIT.  Even if you are
        // not using /WAIT, it will use the info pipe to make sure the process
        // did actually start.

        nfds_t nfds = 0;
        struct pollfd pfds[4];
        unsigned int i;
        ssize_t nbytes;
        size_t inbuf_pos = 0;
        size_t outbuf_capacity = 0;
        size_t errbuf_capacity = 0;

        // Only put the input pipe in the consideration if we can write to
        // it and we have data to send to it.

        if (stdin_pipe[W] > 0 and inbuf_size > 0) {
            /* printf("stdin_pipe[W]: %d\n", stdin_pipe[W]); */
            if (Set_Nonblocking_Fails(stdin_pipe[W]))
                goto kill;

            pfds[nfds].fd = stdin_pipe[W];
            pfds[nfds].events = POLLOUT;
            nfds++;

            close(stdin_pipe[R]);
            stdin_pipe[R] = -1;
        }
        if (stdout_pipe[R] > 0) {
            /* printf("stdout_pipe[R]: %d\n", stdout_pipe[R]); */
            if (Set_Nonblocking_Fails(stdout_pipe[R]))
                goto kill;

            outbuf_capacity = BUF_SIZE_CHUNK;

            outbuf = rebAllocN(char, outbuf_capacity);  // freed if fail()
            outbuf_used = 0;

            pfds[nfds].fd = stdout_pipe[R];
            pfds[nfds].events = POLLIN;
            nfds++;

            close(stdout_pipe[W]);
            stdout_pipe[W] = -1;
        }
        if (stderr_pipe[R] > 0) {
            /* printf("stderr_pipe[R]: %d\n", stderr_pipe[R]); */
            if (Set_Nonblocking_Fails(stderr_pipe[R]))
                goto kill;

            errbuf_capacity = BUF_SIZE_CHUNK;

            errbuf = rebAllocN(char, errbuf_capacity);
            errbuf_used = 0;

            pfds[nfds].fd = stderr_pipe[R];
            pfds[nfds].events = POLLIN;
            nfds++;

            close(stderr_pipe[W]);
            stderr_pipe[W] = -1;
        }

        if (info_pipe[R] > 0) {
            if (Set_Nonblocking_Fails(info_pipe[R]))
                goto kill;

            pfds[nfds].fd = info_pipe[R];
            pfds[nfds].events = POLLIN;
            nfds++;

            infobuf_capacity = 4;

            infobuf = rebAllocN(char, infobuf_capacity);

            close(info_pipe[W]);
            info_pipe[W] = -1;
        }

        int valid_nfds = nfds;
        while (valid_nfds > 0) {
            pid_t xpid = waitpid(
                forked_pid,  // wait for a state change on this process ID
                &status,  // status result (inspect with WXXX() macros)
                WNOHANG  // return immediately (with 0) if no state change
            );

            if (xpid == -1) {
                ret = errno;
                goto error;
            }

            if (xpid == forked_pid) {  // try a last read of remaining out/err
                if (stdout_pipe[R] > 0) {
                    nbytes = read(
                        stdout_pipe[R],
                        outbuf + outbuf_used,
                        outbuf_capacity - outbuf_used
                    );
                    if (nbytes > 0)
                        outbuf_used += nbytes;
                }

                if (stderr_pipe[R] > 0) {
                    nbytes = read(
                        stderr_pipe[R],
                        errbuf + errbuf_used,
                        errbuf_capacity - errbuf_used
                    );
                    if (nbytes > 0)
                        errbuf_used += nbytes;
                }

                if (info_pipe[R] > 0) {
                    nbytes = read(
                        info_pipe[R],
                        infobuf + infobuf_used,
                        infobuf_capacity - infobuf_used
                    );
                    if (nbytes > 0)
                        infobuf_used += nbytes;
                }

                if (WIFSTOPPED(status)) {
                    //
                    // TODO: Review, What's the expected behavior if the
                    // child process is stopped?
                    //
                    continue;
                } else if  (WIFCONTINUED(status)) {
                    // pass
                } else {
                    // exited normally or due to signals
                    break;
                }
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

            for (i = 0; i < nfds and valid_nfds > 0; ++i) {
                /* printf("check: %d [%d/%d]\n", pfds[i].fd, i, nfds); */

                if (pfds[i].revents & POLLNVAL) {  // bad file descriptor
                    assert(!"POLLNVAL received (this should never happen!)");
                    ret = errno;
                    goto kill;
                }

                if (pfds[i].revents & POLLERR) {
                    /* printf("POLLERR: %d [%d/%d]\n", pfds[i].fd, i, nfds); */

                    close(pfds[i].fd);
                    pfds[i].fd = -1;
                    --valid_nfds;
                }
                else if (pfds[i].revents & POLLOUT) {
                    /* printf("POLLOUT: %d [%d/%d]\n", pfds[i].fd, i, nfds); */

                    nbytes = write(
                        pfds[i].fd,
                        inbuf + inbuf_pos,
                        inbuf_size - inbuf_pos
                    );
                    if (nbytes <= 0) {
                        ret = errno;
                        goto kill;
                    }
                    /* printf("POLLOUT: %d bytes\n", nbytes); */
                    inbuf_pos += nbytes;
                    if (inbuf_pos >= inbuf_size) {
                        close(pfds[i].fd);
                        pfds[i].fd = -1;
                        --valid_nfds;
                    }
                }
                else if (pfds[i].revents & POLLIN) {
                    /* printf("POLLIN: %d [%d/%d]\n", pfds[i].fd, i, nfds); */

                    char **buffer;
                    size_t *used;
                    size_t *capacity;
                    if (pfds[i].fd == stdout_pipe[R]) {
                        buffer = &outbuf;
                        used = &outbuf_used;
                        capacity = &outbuf_capacity;
                    }
                    else if (pfds[i].fd == stderr_pipe[R]) {
                        buffer = &errbuf;
                        used = &errbuf_used;
                        capacity = &errbuf_capacity;
                    }
                    else {
                        assert(pfds[i].fd == info_pipe[R]);
                        buffer = &infobuf;
                        used = &infobuf_used;
                        capacity = &infobuf_capacity;
                    }

                    ssize_t to_read = 0;
                    do {
                        to_read = *capacity - *used;
                        assert (to_read > 0);
                        /* printf("to read %d bytes\n", to_read); */
                        nbytes = read(pfds[i].fd, *buffer + *used, to_read);

                        // The man page of poll says about POLLIN:
                        //
                        // "Data other than high-priority data may be read
                        //  without blocking.  For STREAMS, this flag is set
                        //  in `revents` even if the message is of _zero_
                        //  length.  This flag shall be equivalent to:
                        //  `POLLRDNORM | POLLRDBAND`
                        //
                        // And about POLLHUP:
                        //
                        // "A device  has been disconnected, or a pipe or FIFO
                        //  has been closed by the last process that had it
                        //  open for writing.  Once set, the hangup state of a
                        //  FIFO shall persist until some process opens the
                        //  FIFO for writing or until all read-only file
                        //  descriptors for the FIFO  are  closed.  This event
                        //  and POLLOUT are mutually-exclusive; a stream can
                        //  never be writable if a hangup has occurred.
                        //  However, this event and POLLIN, POLLRDNORM,
                        //  POLLRDBAND, or POLLPRI are not mutually-exclusive.
                        //  This flag is only valid in the `revents` bitmask;
                        //  it shall be ignored in the events member."
                        //
                        // So "nbytes = 0" could be a valid return with POLLIN,
                        // and not indicating the other end closed the pipe,
                        // which is indicated by POLLHUP.
                        //
                        if (nbytes <= 0)
                            break;

                        /* printf("POLLIN: %d bytes\n", nbytes); */

                        *used += nbytes;
                        assert(*used <= *capacity);

                        if (*used == *capacity) {
                            char *larger = rebAllocN(
                                char,
                                *capacity + BUF_SIZE_CHUNK
                            );
                            if (larger == nullptr)
                                goto kill;
                            memcpy(larger, *buffer, *capacity);
                            rebFree(*buffer);
                            *buffer = larger;
                            *capacity += BUF_SIZE_CHUNK;
                        }
                        assert(*used < *capacity);
                    } while (nbytes == to_read);
                }

                // A pipe can hangup and also have input (e.g. OS X sets both
                // POLLIN | POLLHUP at once).
                //
                if (pfds[i].revents & POLLHUP) {
                    /* printf("POLLHUP: %d [%d/%d]\n", pfds[i].fd, i, nfds); */
                    close(pfds[i].fd);
                    pfds[i].fd = -1;
                    valid_nfds --;
                }
            }
        }

        if (valid_nfds == 0 and flag_wait) {
            if (waitpid(forked_pid, &status, 0) < 0) {
                ret = errno;
                goto error;
            }
        }
    }

    goto cleanup;

  kill:

    kill(forked_pid, SIGKILL);
    waitpid(forked_pid, nullptr, 0);

  error:

    if (ret == 0)
        non_errno_ret = -1024;  // !!! randomly picked

  cleanup:

    if (info_pipe[R] > 0)
        close(info_pipe[R]);

    if (info_pipe[W] > 0)
        close(info_pipe[W]);

    if (infobuf_used == sizeof(int)) {
        //
        // exec in child process failed, set to errno for reporting.
        //
        ret = *cast(int*, infobuf);
    }
    else if (WIFEXITED(status)) {
        assert(infobuf_used == 0);

       exit_code = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status)) {
        non_errno_ret = WTERMSIG(status);
    }
    else if (WIFSTOPPED(status)) {
        //
        // Shouldn't be here, as the current behavior is keeping waiting when
        // child is stopped
        //
        assert(false);
        if (infobuf)
            rebFree(infobuf);
        rebJumps("fail {Child process is stopped}");
    }
    else {
        non_errno_ret = -2048;  // !!! randomly picked
    }

    if (infobuf != nullptr)
        rebFree(infobuf);

  info_pipe_err:

    if (stderr_pipe[R] > 0)
        close(stderr_pipe[R]);

    if (stderr_pipe[W] > 0)
        close(stderr_pipe[W]);

    goto stderr_pipe_err;  // no jumps to `info_pipe_err:` yet, avoid warning

  stderr_pipe_err:

    if (stdout_pipe[R] > 0)
        close(stdout_pipe[R]);

    if (stdout_pipe[W] > 0)
        close(stdout_pipe[W]);

  stdout_pipe_err:

    if (stdin_pipe[R] > 0)
        close(stdin_pipe[R]);

    if (stdin_pipe[W] > 0)
        close(stdin_pipe[W]);

  stdin_pipe_err:

    // We will get to this point on success, as well as error (so ret may
    // be 0.  This is the return value of the host kit function to Rebol, not
    // the process exit code (that's written into the pointer arg 'exit_code')

    if (non_errno_ret > 0) {
        rebJumps(
            "fail [",
                "{Child process is terminated by signal:}",
                rebI(non_errno_ret),
            "]"
        );
    }
    else if (non_errno_ret < 0)
        rebJumps("fail {Unknown error happened in CALL}");


    // Call may not succeed if r != 0, but we still have to run cleanup
    // before reporting any error...

    assert(argc > 0);

    int i;
    for (i = 0; i != argc; ++i)
        rebFree(argv[i]);

    rebFree(argv);

    if (IS_TEXT(ARG(output))) {
        REBVAL *output_val = rebRepossess(outbuf, outbuf_used);
        rebElide("insert", ARG(output), output_val);
        rebRelease(output_val);
    }
    else if (IS_BINARY(ARG(output))) {  // same (but could be different...)
        REBVAL *output_val = rebRepossess(outbuf, outbuf_used);
        rebElide("insert", ARG(output), output_val);
        rebRelease(output_val);
    }
    else
        assert(outbuf == nullptr);

    if (IS_TEXT(ARG(error))) {
        REBVAL *error_val = rebRepossess(errbuf, errbuf_used);
        rebElide("insert", ARG(error), error_val);
        rebRelease(error_val);
    }
    else if (IS_BINARY(ARG(error))) {  // same (but could be different...)
        REBVAL *error_val = rebRepossess(errbuf, errbuf_used);
        rebElide("insert", ARG(error), error_val);
        rebRelease(error_val);
    }
    else
        assert(errbuf == nullptr);

    if (inbuf != nullptr)
        rebFree(inbuf);

    if (ret != 0)
        rebFail_OS (ret);

    if (REF(info)) {
        REBCTX *info = Alloc_Context(REB_OBJECT, 2);

        Init_Integer(Append_Context(info, nullptr, Canon(ID)), forked_pid);
        if (REF(wait))
            Init_Integer(
                Append_Context(info, nullptr, Canon(EXIT_CODE)),
                exit_code
            );

        return Init_Object(OUT, info);
    }

    // We may have waited even if they didn't ask us to explicitly, but
    // we only return a process ID if /WAIT was not explicitly used
    //
    if (REF(wait))
        return Init_Integer(OUT, exit_code);

    return Init_Integer(OUT, forked_pid);
}

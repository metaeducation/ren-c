//
//  file: %fcntl-patch.c
//  summary: "Use GCC voodoo to undo GLIBC 2.28 `fcntl` redefine to `fcntl64`"
//  section: extension
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2019 Ren-C Open Source Contributors
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
// This file is signaled for the filesystem extension to include by the
// environment variable USE_BACKDATED_GLIBC.  (Environment variables are not
// ideal, but the all-Rebol build system is not very mature yet.)
//
// What it does is use special GCC features to "backdate" an executable built
// on a system with GLIBC 2.28 (or higher), such that it can be used on older
// Linuxes.  The reason such measures are needed is that in the header file
// <fcntl.h>, `fcntl` was remapped via a #define to call a function that is
// actually named fcntl64()...unavailable on Linuxes before October 2018.
//
// Subsequently `pow` and `log` for math were changed in such a way that they
// were linking against 2.29 with some kind of more "optimized" version.  This
// doesn't really have anything to do with the filesystem extension, but
// localizing the hack to this file is more useful at the moment than putting
// it somewhere else.  (Revisit at a later date if needed, though the real
// answer is probably just to switch to `musl`.)  Full list of operations is:
//
//     "...exp, exp2, log, log2, pow, sinf, cosf, sincosf and tanf"
//
// It's hardly ideal to do this.  But for some of the reasoning, please see:
// https://forum.rebol.info/t/1231
//

#define _GNU_SOURCE  // so we get F_OFD_XXX and not just F_XXX
#include <fcntl.h>
#include "c-enhanced.h"

// "symver" does the remapping so that fcntl64 is given the meaning of an
// older linkage in Glibc.
//
// https://stackoverflow.com/q/4032373/
//
asm (".symver fcntl64, fcntl@GLIBC_2.2.5");


// We add to the patch to also backdate pow() and log(), which were updated
// in math from 2.29....
//
asm (".symver pow, pow@GLIBC_2.2.5");
asm (".symver log, log@GLIBC_2.2.5");

EXTERN_C double __wrap_pow(double base, double exponent)
  { return pow(base, exponent); }

EXTERN_C double __wrap_log(double x)
  { return log(x); }


// When the linker switch `--wrap=fcntl64` is used, then all the calls to
// fcntl() that were mapped to fcntl64() will be routed through this function,
// which can then chain through to the remapped implementation of fcntl64()
// (an old fcntl()).
//
// Unfortunately, fcntl is a variadic function which is not easy to wrap.  We
// could do assembly voodoo, but since it is documented how it handles the
// one-or-zero parameters it takes, we can just process the variadic call and
// then make a new one.
//
// Code taken from: https://stackoverflow.com/a/58472959/
//
EXTERN_C int __wrap_fcntl64(int fd, int cmd, ...)
{
    int result;
    va_list va;
    va_start(va, cmd);

    switch (cmd) {
      //
      // File descriptor flags
      //
      case F_GETFD: goto takes_void;
      case F_SETFD: goto takes_int;

      // File status flags
      //
      case F_GETFL: goto takes_void;
      case F_SETFL: goto takes_int;

      // File byte range locking, not held across fork() or clone()
      //
      case F_SETLK: goto takes_flock_ptr;
      case F_SETLKW: goto takes_flock_ptr;
      case F_GETLK: goto takes_flock_ptr;

      // File byte range locking, held across fork()/clone() -- Not POSIX
      //
      case F_OFD_SETLK: goto takes_flock_ptr;
      case F_OFD_SETLKW: goto takes_flock_ptr;
      case F_OFD_GETLK: goto takes_flock_ptr;

      // Managing I/O availability signals
      //
      case F_GETOWN: goto takes_void;
      case F_SETOWN: goto takes_int;
      case F_GETOWN_EX: goto takes_f_owner_ex_ptr;
      case F_SETOWN_EX: goto takes_f_owner_ex_ptr;
      case F_GETSIG: goto takes_void;
      case F_SETSIG: goto takes_int;

      // Notified when process tries to open or truncate file (Linux 2.4+)
      //
      case F_SETLEASE: goto takes_int;
      case F_GETLEASE: goto takes_void;

      // File and directory change notification
      //
      case F_NOTIFY: goto takes_int;

      // Changing pipe capacity (Linux 2.6.35+)
      //
      case F_SETPIPE_SZ: goto takes_int;
      case F_GETPIPE_SZ: goto takes_void;

      // File sealing (Linux 3.17+)
      //
      case F_ADD_SEALS: goto takes_int;
      case F_GET_SEALS: goto takes_void;

      // File read or write hints (Linux 4.13+)
      //
      case F_GET_RW_HINT: goto takes_uint64_t_ptr;
      case F_SET_RW_HINT: goto takes_uint64_t_ptr;
      case F_GET_FILE_RW_HINT: goto takes_uint64_t_ptr;
      case F_SET_FILE_RW_HINT: goto takes_uint64_t_ptr;

      default:
        fail ("fcntl64 dependency workaround got unknown F_XXX constant");
    }

  takes_void:
    va_end(va);
    return fcntl64(fd, cmd);

  takes_int:
    result = fcntl64(fd, cmd, va_arg(va, int));
    va_end(va);
    return result;

  takes_flock_ptr:
    result = fcntl64(fd, cmd, va_arg(va, struct flock*));
    va_end(va);
    return result;

  takes_f_owner_ex_ptr:
    result = fcntl64(fd, cmd, va_arg(va, struct f_owner_ex*));
    va_end(va);
    return result;

  takes_uint64_t_ptr:
    result = fcntl64(fd, cmd, va_arg(va, uint64_t*));
    va_end(va);
    return result;
}

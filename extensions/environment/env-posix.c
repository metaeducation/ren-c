#include <unistd.h>
#include <stdlib.h>

#ifdef USING_LIBREBOL  // need %sys-core.h variation for IMPLEMENT_GENERIC()
    #include <assert.h>
    #include "needful/needful.h"
    #define cast  v_cast
    #define Sink  SinkTypemacro

    #include "rebol.h"
    typedef RebolValue Value;
    typedef Value ErrorValue;
#else
    #include "sys-core.h"
    typedef Value ErrorValue;
#endif

#include "environment.h"


// The location of "environ" (environment variables inventory that you
// can walk on POSIX) can vary.  Some put it in stdlib, some put it
// in <unistd.h>.  And OS X doesn't define it in a header at all, you
// just have to declare it yourself.  :-/
//
// https://stackoverflow.com/a/31347357/211160
//
#if defined(TARGET_OS_MAC) || defined(__OpenBSD__)
    extern char **environ;
#endif


Option(ErrorValue*) Trap_Get_Environment_Variable(
    Sink(Option(Value*)) out,  // ~null~ means not set
    const Value* key  // Note: POSIX mandates case-sensitive keys
){
    char* key_utf8 = rebSpell("@", key);

    const char* val_utf8 = getenv(key_utf8);  // no error conditions or errno
    if (val_utf8 == nullptr)  // key not present in environment
        *out = nullptr;
    else {
        /* assert(strsize(val) != 0); */  // True?  Should it return null?

        *out = rebText(val_utf8);
    }

    rebFree(key_utf8);
    return nullptr;  // success
}


// 1. Note that putenv() IS *FATALLY FLAWED*!
//
//    putenv() takes a single "key=val" string, and takes it *mutably*.  :-(
//    It is obsoleted by setenv() and unsetenv() in System V:
//
//    Once you've passed a string to putenv() you never know when that string
//    will no longer be needed.  Thus it must either not be dynamic or you
//    must leak it, or track a local copy of the environment yourself.
//
//    If you're stuck without setenv on some old platform, but really need to
//    set an environment variable, here's a way that just leaks a string each
//    time you call.  The code would have to keep track of each string added
//    in some sort of a map...which is currently deemed not worth the work.
//
// 2. putenv("NAME") removing the variable from the environment is apparently
//    a nonstandard extension of the GNU C library:
//
//      https://man7.org/linux/man-pages/man3/putenv.3.html
//
//    It does nothing on NetBSD for instance.  Prefer unsetenv() if available:
//
//      http://julipedia.meroh.net/2004/10/portability-unsetenvfoo-vs-putenvfoo.html
//
// 3. The clang static analyzer notices when an allocated pointer is neither
//    used nor freed.  We don't want to free the string in the static analyzer
//    build (it's nice if a static analysis build still "works") so just fool
//    it to thinking we "used" the pointer by putting it in a static variable.
//
Option(ErrorValue*) Trap_Update_Environment_Variable(
    const Value* key,  // Note: POSIX mandates case-sensitive keys
    Option(const Value*) value
){
    Option(ErrorValue*) e = nullptr;

    char* key_utf8 = rebSpell(key);

    if (not value) {
      #ifdef unsetenv  // use unsetenv() if available [1]
        if (unsetenv(key_utf8) == -1)
            e = rebValue(
                "make warning! -[unsetenv() can't unset environment variable]-"
            );
      #else
        int res = putenv(key_utf8);  // GNU-specific: putenv("NAME") unsets [2]
        if (res == -1)
            e = rebValue(
                "make warning! -[putenv() can't unset environment variable]-"
            );
      #endif
    }
    else {
      #ifdef setenv
        char *val_utf8 = rebSpell(value);
        int res = setenv(key_utf8, val_utf8, 1);  // the 1 means "overwrite"
        if (res == -1)
            e = rebValue(
                "make warning! -[setenv() can't set environment variable]-"
            );

        rebFree(val_utf8);
      #else
        char *key_equals_val_utf8 = rebSpell(
            "unspaced [@", key, "#=", unwrap value, "]"
        );

        char *duplicate = strdup(key_equals_val_utf8);

        if (putenv(duplicate) == -1) {  // !!! putenv() holds onto string! [1]
            free(duplicate);
            e = rebValue(
                "make warning! -[putenv() couldn't set environment variable]-"
            );
        }

      #if DEBUG_STATIC_ANALYZING  // trick analyzer to not see leak [3]
        static char* fakeuse = duplicate;
        USED(fakeuse);
      #endif

        rebFree(key_equals_val_utf8);
      #endif
    }

    rebFree(key_utf8);

    return e;
}


// 1. 'environ' is an extern of a global found in <unistd.h>, and each entry
//     contains a `key=value` formatted string.
//
//       https://stackoverflow.com/q/3473692/
//
// 2. It's safe to search for just a `=` byte, since the high bit isn't set...
//    and even if the key contains UTF-8 characters, there won't be any
//    occurrences of such bytes in multi-byte-characters.
//
Option(ErrorValue*) Trap_List_Environment(Sink(Value*) map_out)
{
    Option(ErrorValue*) e = nullptr;

    Value* map = rebValue("to map! []");

    int n;
    for (n = 0; environ[n] != NULL; ++n) {
        const char *key_equals_val = environ[n];
        const char *eq_pos = strchr(key_equals_val, '=');  // utf-8-safe [2]

        size_t size = strlen(key_equals_val);

        int key_size = eq_pos - key_equals_val;
        Value* key = rebSizedText(key_equals_val, key_size);

        int val_size = size - (eq_pos - key_equals_val) - 1;
        Value* val = rebSizedText(eq_pos + 1, val_size);

        rebElide("append", map, "spread [", rebR(key), rebR(val), "]");
    }

    if (e)
        return e;

    *map_out = map;
    return nullptr;  // success
}

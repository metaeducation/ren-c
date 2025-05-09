#define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
#include <windows.h>

// As is typical, Microsoft's own header files don't work through with
// the static analyzer, disable checking of their _In_out_ annotations
// (which we don't use, anyway):
//
//   https://developercommunity.visualstudio.com/t/warning-C6553:-The-annotation-for-functi/1676659

#if defined(_MSC_VER) && defined(_PREFAST_)  // _PREFAST_ if MSVC /analyze
  #pragma warning(disable : 6282)  // suppress "incorrect operator" [1]
#endif

#ifdef USING_LIBREBOL  // need %sys-core.h variation for IMPLEMENT_GENERIC()
    #include <assert.h>
    #include "c-enhanced.h"
    #define Sink SinkTypemacro

    #include "rebol.h"
    typedef RebolValue Value;
    typedef Value ErrorValue;
#else
    #undef OUT
    #undef VOID

    #include "sys-core.h"
    typedef Value ErrorValue;
#endif


#include "environment.h"


// 1. This is tricky, because although GetEnvironmentVariable() says that a 0
//    return means an error, it also says it is the length of the variable
//    minus the terminator (when the passed in buffer is a sufficient size).
//
//    https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getenvironmentvariable
//
//    So if a variable is set-but-empty, then it could return 0 in this second
//    step.  (Who designs such an API?!  Why wouldn't it just consistently
//    return length including the terminator regardless of whether the buffer
//    is big enough or not, so 0 is always an warning?!)
//
//    Such variables can't be assigned with SET, as `set var=` will clear it.
//    But other mechanisms can...including GitHub Actions when it sets up
//    `env:` variables.
//
Option(ErrorValue*) Trap_Get_Environment_Variable(
    Sink(Option(Value*)) out,  // ~null~ means not set
    const Value* key  // Note: Windows is not case-sensitive w.r.t. keys
){
    Option(ErrorValue*) e = nullptr;

    WCHAR *key_wide = rebSpellWide(rebQ(key));

    DWORD val_len_plus_one = GetEnvironmentVariable(key_wide, nullptr, 0);
    if (val_len_plus_one == 0) {  // some failure...
        DWORD dwerr = GetLastError();
        if (dwerr == ERROR_ENVVAR_NOT_FOUND)
            *out = nullptr;
        else
            e = rebError_OS(dwerr);  // don't call GetLastError() twice!
    }
    else {
        WCHAR *val_wide = rebAllocN(WCHAR, val_len_plus_one);
        DWORD val_len = GetEnvironmentVariable(
            key_wide, val_wide, val_len_plus_one
        );

        if (val_len + 1 != val_len_plus_one) {  // "set-but-empty" :-( [1]
            DWORD dwerr = GetLastError();
            if (dwerr == 0) {  // in case this ever happens, give more info
                e = rebValue("make warning! spaced [",
                    "-[Mystery bug getting environment var]- @", key,
                    "-[with length reported as]-", rebI(val_len_plus_one - 1),
                    "-[but returned length from fetching is]-", rebI(val_len),
                "]");
            }
            else
                e = rebError_OS(dwerr);
        }
        else
            *out = rebLengthedTextWide(val_wide, val_len_plus_one - 1);

        rebFree(val_wide);
    }

    rebFree(key_wide);

    return e;
}


Option(ErrorValue*) Trap_Update_Environment_Variable(
    const Value* key,  // Note: Windows is not case-sensitive w.r.t. keys
    Option(const Value*) value
){
  #if RUNTIME_CHECKS
    rebElide("ensure [~null~ text!] @", maybe value);
  #endif

    Option(ErrorValue*) e = nullptr;

    WCHAR* key_wide = rebSpellWide(rebQ(key));
    Option(WCHAR*) val_wide = rebSpellWideMaybe(maybe value);

    if (not SetEnvironmentVariable(
        key_wide,
        maybe val_wide  // null means unset the environment variable
    )){
        e = rebError_OS(GetLastError());
    }

    rebFreeMaybe(maybe val_wide);
    rebFree(key_wide);

    return e;
}


// Windows environment strings are sequential null-terminated strings, with a
// 0-length string signaling end ("keyA=valueA\0keyB=valueB\0\0")  We count
// the strings to know how big an array to make, and then convert the array
// into a MAP!.
//
// 1. "What are these strange =C: environment variables?"
//
//    https://blogs.msdn.microsoft.com/oldnewthing/20100506-00/?p=14133
//
Option(ErrorValue*) Trap_List_Environment(Sink(Value*) map_out)
{
    Option(ErrorValue*) e = nullptr;

    Value* map = rebValue("to map! []");

    WCHAR* env = GetEnvironmentStrings();

    int len;
    const WCHAR* key_equals_val = env;
    while ((len = wcslen(key_equals_val)) != 0) {
        const WCHAR *eq_pos = wcschr(key_equals_val, '=');

        if (eq_pos == key_equals_val) {  // "strange =C: variables" [1]
            key_equals_val += len + 1; // next
            continue;
        }

        int key_len = eq_pos - key_equals_val;
        Value* key = rebLengthedTextWide(key_equals_val, key_len);

        int val_len = len - (eq_pos - key_equals_val) - 1;
        Value* val = rebLengthedTextWide(eq_pos + 1, val_len);

        rebElide("poke", map, rebR(key), rebR(val));

        key_equals_val += len + 1; // next
    }

    FreeEnvironmentStrings(env);

    if (e)
        return e;

    *map_out = map;
    return nullptr;  // success
}

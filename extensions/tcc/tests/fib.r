REBOL [
    Title: {Comparing User Native vs. Rebol Fibonacci Number Calculation}
    Description: {
        @ShixinZeng created this as an initial test to compare the performance
        of a user native implementation of Fibonacci numbers, to an algorithm
        written in the exact same style using Rebol.

        It takes hundreds of CPU cycles to run a single step of Rebol, since
        it is run by an interpreter.  But a line of C generally translates
        into only a few instructions.  At time of writing, the TCC version is
        about 50x faster than an -O2 release build running the Rebol version.
    }
]

c-fib: make-native [
    "nth Fibonacci Number"
    n [integer!]
]{
    int n = rebUnboxInteger(rebArgR("n"));

    if (n < 0) { return rebInteger(-1); }
    if (n <= 1) { return rebInteger(n); }

    int i0 = 0;
    int i1 = 1;
    while (n > 1) {
        int t = i1;
        i1 = i0 + i1;
        i0 = t;
        --n;
    }
    return rebInteger(i1);
}

rebol-fib: function [
    n [integer!]
][
    if n < 0 [return -1]
    if n <= 1 [return n]
    i0: 0
    i1: 1
    loop [n > 1] [
        t: i1
        i1: i0 + i1
        i0: t
        n: n - 1
    ]
    return i1
]

compilables: [
    ;
    ; We get two tests in one here...we test the availability of the
    ; C standard library, and also handling of #define with quotes
    ; embedded in them (it's a tricky case that trips up mbedTLS if
    ; you don't support it correctly).
    ;
    {#include STDIO_INCLUDE} 

    c-fib
]

opts: [
    ; This can be specified with LIBREBOL_INCLUDE_DIR as an environment
    ; variable, but you can also do it here for convenience.
    ;
    ;;librebol-path %/home/hostilefork/Projects/ren-c/build/prep/include/

    ; This can be specified with CONFIG_TCCDIR as an environment variable,
    ; but you can also do it here for convenience.  Lack of a trailing
    ; slash is tolerated as that is CONFIG_TCCDIR convention in TCC.
    ;
    ;;runtime-path %/home/hostilefork/Projects/tcc

    ; TCC expects options strings to have the backslashes in them.  This
    ; is needed because plain quotes are used for filenames that have
    ; spaces in them.  Note that the C99 command line emulation must
    ; therefore add these quotes in for #defines, since the shell will
    ; strip out any backslashes provided.
    ;
    options {-DSTDIO_INCLUDE=\"stdio.h\"}
]

compile/inspect/settings compilables opts  ; print out for verbose info
compile/settings compilables opts  ; does the actual compilation

print ["c-fib 30:" c: c-fib 30]
print ["rebol-fib 30:" r: rebol-fib 30]

assert [c = r]

if not find system/options/args "nobench" [
    n: 10000
    print ["=== Running benchmark," n "iterations ==="]
    print "(If you're using a debug build, this metric is affected)"

    c: delta-time [
        repeat n [c-fib 30]
    ]
    r: delta-time [
        repeat n [rebol-fib 30]
    ]

    print ["C time:" c]
    print ["Rebol time:" r]
    print ["Improvement:" unspaced [to integer! (r / c) "x"]]
]

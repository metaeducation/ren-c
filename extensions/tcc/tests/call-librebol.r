REBOL [
    Title: {Test C Code Using Librebol Instead of Stdio.h}
    Description: {
        In order to call C functions like printf(), you must have headers and
        libraries installed on your ARM platform.  This is difficult to do
        without something like BusyBox that establishes a sandbox environment
        where you install the whole GCC toolchain.

        You can certainly do that with Ren-C, as demonstrated in this video:

        https://www.youtube.com/watch?v=r5kccBehMMg

        But another alternative is to use the built-in ability for the TCC
        extension to make calls into the host executable, and ask Ren-C
        itself to provide the functionality.  So instead of:

            int value = 10;
            printf("The value is %d\n", value);

        You would call into the interpreter:

            int value = 10;
            rebElide("print [{The value is}", rebI(value), "]");

        The API provides memory alloction through rebAlloc() and rebFree(), so
        the code you write isn't necessarily completely trivial.  But to be
        realistic, you are probably going to need functions like memcpy() and
        other standard functions.  So this is just a proof of concept.
    }
]


CONFIG_TCCDIR: switch length of system.options.args [
    0 []
    1 [local-to-file first system.options.args]
    fail "call-librebol.r takes an optional CONFIG_TCCDIR as an argument."
]


call-librebol: make-native [
    "Show It's Possible to Call libRebol without GNU toolchain installed"
    n [integer!]
]{
    int n = rebUnboxInteger(rebArgR("n"));
    rebElide("print [{Hello, libRebol World:}", rebI(n), "]");
    return rebInteger(n + 20);
}


compilables: [
    call-librebol
]

opts: compose [
    ; This can be specified with LIBREBOL_INCLUDE_DIR as an environment
    ; variable, but you can also do it here for convenience.
    ;
    ;;librebol-path %/home/hostilefork/Projects/ren-c/build/prep/include/

    ; This can be specified with CONFIG_TCCDIR as an environment variable,
    ; but you can also do it here for convenience.  Lack of a trailing
    ; slash is tolerated as that is CONFIG_TCCDIR convention in TCC.
    ;
    ((if CONFIG_TCCDIR [compose [
        runtime-path (CONFIG_TCCDIR)  ; e.g. %/home/hostilefork/Projects/tcc
    ]]))

]

; libRebol depends on very basic definitions like `bool`, `true`, `false`,
; and `uintptr_t`.  It defaults to including <stdlib.h> and <stdint.h>
; and <stdbool.h> to get these.  But if your system doesn't have these
; things (because there's no toolchain) then that dependency won't be
; available.
;
; The point of this file is to demonstrate the ability to offer compilation
; and user interface for C in spite of that, with only the r3 executable
; (and its limited encapped headers for implementing intrinsics, as well
; as its small runtime library for those intrinsics).
;
; So we use the /nostdlib option
;
compile/inspect/settings/nostdlib compilables opts  ; print out for verbose info
compile/settings/nostdlib compilables opts  ; does the actual compilation

print "COMPILE SUCCEEDED"

result: call-librebol 1000
print [{The result was:} result]

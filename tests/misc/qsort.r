Rebol [
    Title: "Callback demonstration for FFI extension: Quicksort"
    File: %qsort.r

    Description: {
        qsort() is defined in the standard C library in <stdlib.h>.  It can be
        used to sort an array of arbitrary data by passing it a pointer to a
        C function which serves as the comparison operator.  Using only the
        logical result of the comparison, the fixed-size elements of the
        array can be put in order by the generic quicksort algorithm.

        https://en.wikipedia.org/wiki/Qsort

        An FFI "routine" can provide a bridge function for Rebol to invoke
        qsort(), translating Rebol datatypes to corresponding values that can
        be read by the C code.  *But* the comparison operation needs to be
        passed to qsort() as a C function pointer.  Because while the C code
        is running, it doesn't know how to invoke the Rebol interpreter to
        run an ACTION! directly.

        LibFFI provides a "closure" facility, which allows the dynamic
        creation of an artificial C function pointer at runtime:

        http://www.chiark.greenend.org.uk/doc/libffi-dev/html/The-Closure-API.html

        The Rebol FFI interface uses this in WRAP-CALLBACK, which lets a
        Rebol function be called from C, with particular expectations of the
        C data types used to invoke it.  The parameter language and supported
        types used is the same as that in MAKE-ROUTINE.
    }

    Notes: {
        The C language does not have strict typing rules for the arguments
        to functions passed by pointer.  This means when a function takes a
        function pointer as an argument, there's not enough information in
        that function's annotated specification to automatically "callbackify"
        a Rebol ACTION! used as an argument.

        While Rebol's routine spec language could try and remedy this by
        forcing function pointer arguments to state precise typing, this
        would be at odds with how the language may work.  e.g. another one
        of the function's parameters may dictate the choice of what type of
        parameter the callback receives.
    }

    See-Also: {
        "user natives", which embed a TCC compiler into the Rebol
        executable.  This provides an alternative for those who would prefer
        to write their callbacks directly in C, yet still include that C
        source in a Rebol module.
    }
]

recycle/torture

f: function [
    a [integer!]
    b [integer!]
][
    i: make struct! compose/deep [
        [raw-memory: (a)]
        i [int32]
    ]
    j: make struct! compose/deep [
        [raw-memory: (b)]
        i [int32]
    ]
    case [
        i/i == j/i [0]
        i/i < j/i [-1]
        i/i > j/i [1]
    ]
]

; This test uses WRAP-CALLBACK and not MAKE-CALLBACK (which creates the
; function and wraps it in one step).  This is in order to be easier to
; debug if something is going wrong.
;
cb: wrap-callback :f [
    return: [int64]
    a [pointer]
    b [pointer]
]

libc: make library! %libc.so.6

x64?: 40 == fifth system/version
size_t: either x64? ['int64]['int32]

qsort: make-routine libc "qsort" compose/deep [
    base [pointer]
    nmemb [(size_t)]
    size [(size_t)]
    comp [pointer]
]

array: make vector! [integer! 32 5 [10 8 2 9 5]]
print ["before:" mold array]
probe (addr-of :cb)
qsort array 5 4 (addr-of :cb)
print ["after:" mold array]

assert [array == make vector! [integer! 32 5 [2 5 8 9 10]]]

close libc

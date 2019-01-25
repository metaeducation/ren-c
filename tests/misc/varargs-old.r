REBOL []

recycle/torture

libc: switch fourth system/version [
    3 [
        make library! %msvcrt.dll
    ]
    4 [
        make library! %libc.so.6
    ]
]

printf: make routine! [
    [
        "An example of wrapping variadic functions"
        fmt [pointer] "fixed"
        ... "variadic"
        return: [int32]
    ]
    libc "printf"
]

sprintf: make routine! [
    [
        "An example of wrapping variadic functions"
        buf [pointer] "destination buffer, must be big enough"
        fmt [pointer] "fixed"
        ... "variadic"
        return: [int32]
    ]
    libc "sprintf"
]

i: 1000
j: 0.0
printf reduce [
    join "i: %d, %f" newline
    i [int32]
    j [float]
]

printf compose [
    "hello %p%c"
    ;10.0
    "ffi" [pointer]
    ;"ffi"
    (to integer! newline) [int8]
]

printf compose [
    "hello %s%c"
    "world" [pointer]
    (to integer! newline) [int8]
]

printf compose [
    "hello %s%c"
    "ffi" [pointer]
    (to integer! newline) [int8]
]

h: make struct! [
    a [uint8 [128]]
]
len: sprintf reduce [
    addr-of h
    join "hello %s" newline
    "world" [pointer]
]
prin ["h:" copy/part to text! values of h len]

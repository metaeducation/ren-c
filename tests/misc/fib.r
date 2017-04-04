REBOL [
    Title: {Initial test for user natives by @ShixinZeng}
]

c-fib: make-native [
   "nth Fibonacci Number"
   n [integer!]
]{
    int n = VAL_INT64(ARG(n));

    if (n < zero) { Init_Integer(D_OUT, -1); return R_OUT; }
    if (n <= one) { Init_Integer(D_OUT, n); return R_OUT; }

    int i0 = zero;
    int i1 = one;
    while (n > one) {
        int t = i1;
        i1 = i1 + i0;
        i0 = t;
        --n;
    }
    Init_Integer(D_OUT, i1);
    return R_OUT;
}

compile/options [
    "const int zero = 0;"
    "const int one = 1;"
    c-fib
] compose [
    runtime-path (join-of (first split-path system/options/boot) %tcc)
]

fib: func [
    n [integer!]
][
    if n < 0 [return -1]
    if n <= 1 [return n]
    i0: 0
    i1: 1
    while [n > 1] [
        t: i1
        i1: i0 + i1
        i0: t
        -- n
    ]
    i1
]

print ["c-fib 30:" c-fib 30]
print ["fib 30:" fib 30]

n-loop: 10000

c-t: dt [
    loop n-loop [c-fib 30]
]
r-t: dt [
    loop n-loop [fib 30]
]
print ["c-t:" c-t "r-t:" r-t "improvement:" r-t / c-t]

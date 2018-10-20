REBOL [
    Title: {Initial test for user natives by @ShixinZeng}
]

c-fib: make-native [
    "nth Fibonacci Number"
    n [integer!]
]{
    int n = rebUnboxInteger(ARG(n), rebEND);

    /* use `zero` and `one` to demonstrate providing constants via COMPILE */
    if (n < zero) { return rebInteger(-1); }
    if (n <= one) { return rebInteger(n); }

    int i0 = zero;
    int i1 = one;
    while (n > one) {
        int t = i1;
        i1 = i1 + i0;
        i0 = t;
        --n;
    }
    return rebInteger(i1);
}

compile/options [
    "const int zero = 0;"
    "const int one = 1;"
    c-fib
] compose [
    options "-nostdlib"
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
        n: n - 1
    ]
    i1
]

print ["c-fib 30:" c-r: c-fib 30]
print ["fib 30:" r: fib 30]
assert [r == c-r]

if find system/options/args "bench" [
    n-loop: 10000

    c-t: delta-time [
        loop n-loop [c-fib 30]
    ]
    r-t: delta-time [
        loop n-loop [fib 30]
    ]
    print ["c-t:" c-t "r-t:" r-t "improvement:" r-t / c-t]
]

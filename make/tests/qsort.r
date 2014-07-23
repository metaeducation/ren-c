REBOL []

recycle/torture


f: func [
    a [integer!] "pointer to an integer"
    b [integer!] "pointer to an integer"
][
    i: make struct! compose/deep [
        [raw-memory: (a)]
        int32 i
    ]
    j: make struct! compose/deep [
        [raw-memory: (b)]
        int32 i
    ]
    case [
        i/i = j/i [0]
        i/i < j/i [-1]
        i/i > j/i [1]
     ]
]
    
cb: make callback! compose [
    [
		a [pointer]
		b [pointer]
		return: [int32]
    ]
    (:f)
]

libc: make library! %libc.so.6

x64?: 40 = fifth system/version
size_t: either x64? ['int64]['int32]
qsort: make routine! compose/deep [
    [
        base [pointer]
        nmemb [(size_t)]
		size [(size_t)]
        comp [pointer]
    ]
    (libc) "qsort"
]

array: make vector! [integer! 32 5 [10 8 2 9 5]]
print ["array:" mold array]
qsort array 5 4 (reflect cb 'addr)
print ["array:" mold array] ; [2 5 8 9 10]

close libc

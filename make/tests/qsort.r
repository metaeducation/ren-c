REBOL []

recycle/torture

i386?: 4 = fifth system/version

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

size_t: either i386? ['int32]['int64]
qsort: make routine! compose/deep [
    [
        base [pointer]
        nmemb [(size_t)]
		size [(size_t)]
        comp [pointer]
    ]
    (libc) "qsort"
]

array: make vector! [integer! 32 5]
array/1: 10
array/2: 8
array/3: 2
array/4: 9
array/5: 5
print ["array:" mold array]
qsort array 5 4 (reflect cb 'addr)
print ["array:" mold array] ; [2 5 8 9 10]

; %load.test.reb

[#20
    ([1] = load "1")
]
[#22 ; a
    ((quote the :a) = load-value "':a")
]
[#22 ; b
    (error? trap [load-value "':a:"])
]
[#858 (
    a: [ < ]
    a = load-value mold a
)]
(error? trap [load "1xyz#"])

; LOAD/NEXT removed, see #1703
;
(error? trap [load/next "1"])


[#1122 (
    any [
        error? trap [load "9999999999999999999"]
        greater? load-value "9999999999999999999" load-value "9223372036854775807"
    ]
)]
; R2 bug
(
     x: 1
     x: load/header "" 'header
     did all [
        x = []
        null? header
     ]
)

[#1421 (
    did all [
        error? trap [load "[a<]"]
        error? trap [load "[a>]"]
        error? trap [load "[a+<]"]
        error? trap [load "[1<]"]
        error? trap [load "[+a<]"]
    ]
)]

([] = load " ")
([1] = load "1")
([[1]] = load "[1]")
([1 2 3] = load "1 2 3")
([1 2 3] = load/type "1 2 3" null)
([1 2 3] = load "rebol [] 1 2 3")
(
    d: load/header "rebol [] 1 2 3" 'header
    all [
        object? header
        [1 2 3] = d
    ]
)

; This was a test from the %sys-load.r which trips up the loading mechanic
; (at time of writing).  LOAD thinks that the entirety of the script is the
; "rebol [] 1 2 3", and skips the equality comparison etc. so it gets
; loaded as [1 2 3], which then evaluates to 3.  The test framework then
; considers that "not a logic".
;
; ([1 2 3] = load "rebol [] 1 2 3")

; File variations:
(equal? read %./ load %./)
(
    write %test.txt s: "test of text"
    s = load %test.txt
)
(
    save %test1.r 1
    1 = load-value %test1.r
)
(
    save %test2.r [1 2]
    [1 2] = load %test2.r
)
(
    save/header %test.r [1 2 3] [title: "Test"]
    [1 2 3] = load %test.r
)
(
    save/header %test-checksum.r [1 2 3] [checksum: true]
    [1 2 3] = load %test-checksum.r
)
(
    save/header %test-checksum.r [1 2 3] [checksum: true compress: true]
    [1 2 3] = load %test-checksum.r
)
(
    save/header %test-checksum.r [1 2 3] [checksum: script compress: true]
    [1 2 3] = load %test-checksum.r
)

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

~scan-invalid~ !! (load "1xyz#")

; LOAD/NEXT removed, see #1703
;
~bad-parameter~ !! (load/next "1")


[#1122 (
    any [
        error? sys.util.rescue [load "9999999999999999999"]
        greater? load-value "9999999999999999999" load-value "9223372036854775807"
    ]
)]

(
    x: ~
    [x header]: load ""
    all [
        x = []
        null? header
    ]
)

[#1421
    ~scan-invalid~ !! (load "[a<]")
    ~scan-invalid~ !! (load "[a>]")
    ~scan-invalid~ !! (load "[a+<]")
    ~scan-invalid~ !! (load "[1<]")
    ~scan-invalid~ !! (load "[+a<]")
]

([] = load " ")
([1] = load "1")
([[1]] = load "[1]")
([1 2 3] = load "1 2 3")
([1 2 3] = load/type "1 2 3" null)
([1 2 3] = load "rebol [] 1 2 3")
(
    [d header]: load "rebol [] 1 2 3" 'header
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
    [test of text] = load %test.txt
    elide delete %test.txt
)
(
    save %test1.r 1
    1 = load-value %test1.r
    elide delete %test1.r
)
(
    save %test2.r [1 2]
    [1 2] = load %test2.r
    elide delete %test2.r
)
(
    save/header %test.r [1 2 3] [title: "Test"]
    [1 2 3] = load %test.r
    elide delete %test.r
)
(
    save/header %test-checksum.r [1 2 3] [checksum: true]
    [1 2 3] = load %test-checksum.r
    elide delete %test-checksum.r
)
(
    save/header %test-checksum.r [1 2 3] [checksum: true compress: true]
    [1 2 3] = load %test-checksum.r
    elide delete %test-checksum.r
)
(
    save/header %test-checksum.r [1 2 3] [checksum: script compress: true]
    [1 2 3] = load %test-checksum.r
    elide delete %test-checksum.r
)

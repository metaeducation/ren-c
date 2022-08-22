; TRANSCODE is an operation which exposes the scanner, and is the basis of LOAD
; It lets you take in UTF-8 text and gives back a BLOCK! of data
;
; The interface is historically controversial for its complexity:
; https://github.com/rebol/rebol-issues/issues/1916
;
; Ren-C attempts to make it easier.  Plain TRANSCODE with no options will simply
; load a string or binary of UTF-8 in its entirety as the sole return result.
; The multiple-return-awareness kicks it into a more progressive mode, so that
; it returns partial results and can actually give a position of an error.

; Default is to scan a whole block's worth of values
([1 [2] <3>] = transcode "1 [2] <3>")

; When asking for a block's worth of values, an empty string gives empty block
(did all [
    result: transcode ""
    [] = result
    not new-line? result
])
(did all [
    result: transcode "^/    ^/    ^/"
    [] = result
    new-line? result
])

; Requesting a position to be returned is implicitly a request to go value
; by value...
(
    did all [
        1 = [value pos]: transcode "1 [2] <3>"
        value = 1
        pos = " [2] <3>"

        [2] = [value pos]: transcode pos
        value = [2]
        pos = " <3>"

        <3> = [value pos]: transcode pos
        value = <3>
        pos = ""

        null = [value pos]: transcode pos
        value = null
        pos = ""
    ]
)
(
    ; Same as above, just using /NEXT instead of multiple returns
    ; Test used in shimming older Ren-Cs
    ;
    did all [
        1 = transcode/next "1 [2] <3>" 'pos
        pos = " [2] <3>"

        [2] = transcode/next pos 'pos
        pos = " <3>"

        <3> = transcode/next pos 'pos
        pos = ""

        null = transcode/next pos 'pos
        pos = ""
    ]
)

(
    [value pos]: transcode "[^M^/ a] b c" except e -> [
        e.id = 'illegal-cr
    ]
)

(did all [
    1 = transcode/next to binary! "1" 'pos
    pos = #{}
])

(
    str: "Cat😺: [😺 😺] (😺)"

    did all [
        'Cat😺: = [value pos]: transcode str
        set-word? value
        value = 'Cat😺:
        pos = " [😺 😺] (😺)"

        [[😺 😺] (😺)] = value: transcode pos  ; no position, read to end
        block? value
        value = [[😺 😺] (😺)]  ; no position out always gets block
    ]
)

; Do the same thing, but with UTF-8 binary...
(
    bin: as binary! "Cat😺: [😺 😺] (😺)"
    bin =  #{436174F09F98BA3A205BF09F98BA20F09F98BA5D2028F09F98BA29}

    did all [
        'Cat😺: = [value pos]: transcode bin
        set-word? value
        value = 'Cat😺:
        pos = #{205BF09F98BA20F09F98BA5D2028F09F98BA29}
        (as text! pos) = " [😺 😺] (😺)"

        [[😺 😺] (😺)] = value: transcode pos  ; no position, read to end
        block? value
        value = [[😺 😺] (😺)]  ; no position out always gets block
    ]
)

; Test for "blackhole" functionality
[
    (10 = set # 10)  ; thrown away
    ([abc def] = [# _]: transcode "abc def")  ; means /NEXT is NULL
    ('abc = [# #]: transcode "abc def")  ; /NEXT is # so truthy, SET ignores
    (raised? [# #]: transcode "3o4")
    ('scan-invalid = pick trap [[# #]: transcode "3o4"] 'id)
]

(
    [v p]: transcode to binary! "7-Feb-2021/23:00"
    [7-Feb-2021/23:00 #{}] = reduce [v p]
)

[
    ('scan-invalid = pick trap [transcode "2022:"] 'id)
    ('scan-invalid = pick trap [transcode ":2022"] 'id)
    ('scan-invalid = pick trap [transcode "^^2022"] 'id)  ; escaped
    ('scan-invalid = pick trap [transcode "@2022"] 'id)
]

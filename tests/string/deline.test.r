; Ren-C tries to bring more method to the madness of Rebol2/R3-Alpha's
; attempt to standardize the internal string format for the language to a
; single codepoint (line feed) to represent newlines.
;
; The basis of this method is to allow carriage returns in strings at
; a mechanical level, but to put in various speed bumps on the edges
; that increasingly prohibit them from entering the system.  Rather than
; try and do "magic", it coaxes users into the world of LF-only files
; (even on Windows)... and for cases where that is not possible it uses
; errors that guide the user to become involved on any needed filtering
; or mutation explicitly at the source level:
;
; https://forum.rebol.info/t/1264/2


; CR codepoints (^M) are ILLEGAL in TO-conversion unless :RELAX is used.
; CR codepoints (^M) are LEGAL in AS-conversion unless :STRICT is used.
[
    (
        str: "a^M^/b"
        a-bin: as blob! str  ; "remembers it was utf-8, optimizes!"
        t-bin: copy a-bin  ; "makes dissociated/unconstrained copy"
        ok
    )

    ('illegal-cr = pick rescue [to text! t-bin] 'id)
    ('illegal-cr = pick rescue [to-text t-bin] 'id)
    (str = to-text:relax t-bin)

    ('illegal-cr = pick rescue [to text! a-bin] 'id)
    ('illegal-cr = pick rescue [to-text a-bin] 'id)
    (str = to-text:relax a-bin)

    (str = as text! t-bin)
    (str = as-text t-bin)
    ('illegal-cr = pick rescue [as-text:strict t-bin] 'id)

    (str = as text! a-bin)
    (str = as-text a-bin)
    ('illegal-cr = pick rescue [as-text:strict a-bin] 'id)
]

; #{00} bytes are illegal in strings regardless of :RELAX or :STRICT
[
    ~illegal-zero-byte~ !! (to text! #{00})
    ~illegal-zero-byte~ !! (to-text #{00})
    ~illegal-zero-byte~ !! (to-text:relax #{00})

    ~illegal-zero-byte~ !! (as text! #{00})
    ~illegal-zero-byte~ !! (as-text #{00})
    ~illegal-zero-byte~ !! (as-text:strict #{00})
]

; Ren-C DELINE allows either all LF or all CR LF
; (Rationale: enforce sanity, and do not disincentivize people from
; "upgrading" CR LF files to just LF for fear of breaking scripts
; that had thrown in DELINE for tolerance.)
[
    (
        str: "^M^/"
        all [
            "^/" = deline str
            "^/" = str  comment "Modifies"
        ]
    )

    ("^/" = deline "^/")

    ('illegal-cr = pick rescue [deline "^M"] 'id)
    ('mixed-cr-lf-found = pick rescue [deline "a^/b^M^/c"] 'id)
]

; Ren-C ENLINE is strict about requiring no CR on the input string
[
    ("^M^/" = enline "^/")
    ("a^M^/b" = enline "a^/b")
    ("a^M^/b^M^/" = enline "a^/b^/")
    ("^M^/a^M^/b" = enline "^/a^/b")

    ('illegal-cr = pick rescue [enline "^M"] 'id)
    ('illegal-cr = pick rescue [enline "^M^/"] 'id)
    ('illegal-cr = pick rescue [enline "^/^M"] 'id)
]

[
    (
        comment "WRITE of TEXT! disallows CR by default"
        'illegal-cr = pick rescue [write %enlined.tmp enline "a^/b"] 'id
    )
    (
        comment "Bypass by writing BLOB!, *but* ENLINE modifies"
        str: "a^/b"
        write %enlined.tmp as blob! enline str
        all [
            #{610D0A62} = read %enlined.tmp
            str = "a^M^/b"
        ]
    )
    (
        comment "WRITE-ENLINED doesn't modify, easy interface"
        str: "a^/b"
        write-enlined %enlined.tmp str
        all [
            #{610D0A62} = read %enlined.tmp
            str = "a^/b"
        ]
    )

    ('illegal-cr = pick rescue [read:string %enlined.tmp] 'id)
    ('illegal-cr = pick rescue [to text! read %enlined.tmp] 'id)
    ("a^M^/b" = as text! read %enlined.tmp)
    ("a^/b" = deline read %enlined.tmp)
]

; The scanner expects files to be in the LF-only format
[
    ('illegal-cr = pick rescue [do "Rebol [] 1^M^/+ 2"] 'id)
    ('illegal-cr = pick rescue [load "1^M^/+ 2"] 'id)
    ('illegal-cr = pick rescue [load unspaced ["{a" "^M^/" "b}"]] 'id)
    ({a^M^/b} = transcode:one "{a^^M^^/b}")
]


[#648
    (["a"] = deline:lines "a")
]
[#1794
    (1 = length of deline:lines "Slovenščina")
]

[https://github.com/metaeducation/ren-c/issues/923
    (
        a: copy #{60}
        count-up 'i 16 [
            append a a
            deline to-text a
        ]
        all [
            (length of a) = 65536
            every 'b a [b = 96]
        ]
    )
]

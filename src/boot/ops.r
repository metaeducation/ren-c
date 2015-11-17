REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Weird Words list for bootstrap to append to lib context"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        This contains the weird words that the lexer doesn't easily
        let us make set-words out of, like /: and //: and <>:
        It turns out there can be a big big difference between:

            <>: func [...] [...]

        and:

            set (bind/new first [<>] bind? 'func) func [...] [...]

        You may be able to get an assignment with the second.  BUT
        it could be too late for references that have already been
        loaded unbound, if those references existed within the same
        module that planned on defining them for export.

        Hence this just lists those 7 words, and during the boot
        process they are injected into the lib context prior to
        running the mezzanine code in base-infix.r that wishes to
        assign functionality to them.
    }
]


; Pretty much everyone expects these to be legal normal words in the future
; (so '<=: 10' would be legal and normal, for instance.)  TAG! naturals will
; simply be disallowed from having their first character or last character
; be a space.  They also won't be able to start with <<, <=, <~ nor will they
; be able to end with >>, => or ~>

<
<=
>
>=


; The somewhat unusual looking "diamond" shape feels like it should be
; owned by TAG!...which is to say anything that starts with a less than
; and ends in a greater than should be one.  Given that contention along
; with that != already exists (and is more familiar to nearly all
; programmers) suggests it should eventually be retaken for the empty tag.
; A transitional step would likely just make it illegal so people can
; convert their instances to !=

<>


; Using slash as a WORD! is very problematic in Rebol, because of its use
; in PATH!.  There isn't much in the way of satisfying alternatives for
; division, *but* that could lead to an interesting possibility of having
; the dispatch of numeric types in paths do division.  So 10/20 could
; evaluate to 0.5 (for instance).  The alternative would be to use the
; divide operation or to make one's own infix operator that was not slash,
; but overloading slash is just not a good idea.

/
//

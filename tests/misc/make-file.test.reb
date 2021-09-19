; make-file.test.reb
;
; The MAKE-FILE facility is an experimental testbed for trying to use the
; new forms of PATH! and TUPLE! in an effective way to create filenames.
;
; Because it is used by bootstrap, the implementation currently has to
; accomodate systems that do not have generalized TUPLE! or PATH!.  They
; use only the block form, e.g. `MAKE-FILE [(dir) / file.txt]`
;
; See the documentation in %make-file.r for the key design premises, which
; involve some level of sanity checking spliced path components.


; In newer Ren-C the operator `%%` is available as a quoting form, and is
; used for brevity in this test file.
[
    (%*.txt = make-file '*.(if true ["txt"]))

    (%*.txt = %% *.(if true ["txt"]))
]


; Inputs that do not have any GROUP! or BLOCK! in them just pass through.
[
    (%a = %% a)
    (%a/b/c = %% a/b/c)
    (%a.b.c = %% a.b.c)
    (%/b/c = %% /b/c)
    (%.b.c = %% .b.c)
]


; A design goal of MAKE FILE! is to use structural knowledge to help sanity
; check path construction.  As a ground-zero example of usefulness, when
; filling in a TUPLE! segment you can't put paths in it.
[
    (
        extension: "txt/bad"

        e: trap [%% a/b.(extension)]
        e.id = 'embedded-file-slash
    )
]


; The BLOCK! form of MAKE FILE! pushes parts together in an unspaced fashion,
; and will run GROUP!s.  At this level, the protection is against doubled-up
; slashes or dots, but that's its only structural guard.
[
    (%a/b/c = %% [a / b / c])
    (%a/b/c = %% [a/b / c])
    (%a/b/c = %% [a/b /c])
    (%a/b/c = %% [a /b/c])

    (%a/b/c/d/e/f = %% [a/b / c/d / e/f])

    (%/b/c = %% [(if false ['a]) /b/c])
    (%a/b/c = %% [(if true ['a]) /b/c])

    (
        e: trap [
            %% [(if true ['a/b/]) /b/c]
        ]
        e.id = 'doubled-file-slash
    )

    (
        e: trap [
            %% [(if true ["a/b/"]) /b/c]
        ]
        e.id = 'embedded-file-slash
    )
]

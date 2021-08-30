; %file-port.test.reb
;
; There was basically no testing of the R3-Alpha file port, and so in
; addition to its various nebulous semantics, it was extremely buggy.
; As a first attack to improve matters, Ren-C is putting aside most of
; the semantic questions just to get what was somewhat defined to be
; working robustly.  This is helped by being able to build on top of
; a single API cross-platform: libuv.

; === DELETE SCRATCH DIRECTORY FROM PREVIOUS TESTS RUNS ===
;
; The tests are performed in the scratch directory.  So as a first step
; we try to delete the directory if it exists.  Then in case there was no
; directory, make some simple small files and then delete them just to
; test that up front.
[
    (delete-recurse: func [f [file!]] [
        if not exists? f [
            fail [f "does not exist"]
        ]
        if not dir? f [
            delete f
            return true
        ]
        for-each item read f [
           name: join f item
           delete-recurse name
        ]
        delete f
        return true
    ]
    true)

    (if exists? %scratch/ [delete-recurse %scratch/], true)  ; first call
    (not exists? %scratch/)  ; should be gone

    ; Now let's fake up a directory as if it had been leftover from
    ; a previous test run...

    (create %scratch/, true)
    ('dir = exists? %scratch/)
    ([] = read %scratch/)

    (
        write %scratch/leftover1.txt "leftover 1"
        "leftover 1" = as text! read %scratch/leftover1.txt
    )
    (
       write %scratch/leftover2.txt "leftover 2"
       "leftover 2" = read/string %scratch/leftover2.txt
    )
    ('file = exists? %scratch/leftover1.txt)
    ('file = exists? %scratch/leftover2.txt)
    ;
    ; Platform variations in file contents, so sort to be sure.
    ;
    ((sort copy [%leftover1.txt %leftover2.txt]) = sort read %scratch/)

    (create %scratch/leftover-dir/, true)
    ('dir = exists? %scratch/leftover-dir/)
    ([] = read %scratch/leftover-dir/)

    (
       write %scratch/leftover-dir/leftover3.txt "leftover 3"
       "leftover 3" = as text! read %scratch/leftover-dir/leftover3.txt
    )
    ('file = exists? %scratch/leftover-dir/leftover3.txt)

    (delete-recurse %scratch/)  ; second call
    (not exists? %scratch/)
]

; We used CREATE above to make a directory, now try MAKE-DIR
[
    (make-dir/deep %scratch/sub1/sub2/, true)

    ('dir = exists? %scratch/sub1/)
    ('dir = exists? %scratch/sub1/sub2/)

    ([%sub1/] = read %scratch/)
    ([%sub2/] = read %scratch/sub1/)
    ([] = read %scratch/sub1/sub2/)

    (change-dir %scratch, true)
]

; === EMPTY FILE TESTS ===
[
    (
        p: open/new %empty.dat
        close p
        'file = exists? %empty.dat
    )
    (null = read %empty.dat)
    (null = read/part %empty.dat 100)
    (
        p: open %empty.dat
        did all [
            null = read p
            null = read/part p 100
            0 = length of p
        ]
        elide close p
    )
    (
        p: open %empty.dat
        did all [
            (0 = offset of p)
            (elide skip p 100)
            (100 = offset of p)
            (e: trap [read p], e.id = 'out-of-range)
            (e: trap [read/part p 100], e.id = 'out-of-range)

            (elide skip p -100)
            (0 = offset of p)
            (null = read p)
            (null = read/part p 100)

            ; !!! The libuv file indices are unsigned 64-bit integers.  We
            ; could use signed integers and allow you to go negative like
            ; series do, with the idea that you might do skipping calculations
            ; that would eventually add up to being validly positive...and
            ; just be capable of holding an intermediate neative unreadable
            ; state.  But since we don't do that, skipping into negative
            ; territory must either error or clip.  For now, be conservative
            ; and clip.
            ;
            (e: trap [elide skip p -100], e.id = 'out-of-range)
        ]
        elide close p
    )
]

; === SMALL FILE TESTS ===
[
    (
        p: open/new %small.dat
        write p "He"
        write/part p "lloDECAFBAD" 3
        write p space
        write p as binary! "Wo"
        write/part p as binary! "rldBAADF00D" 3
        close p
        'file = exists? %small.dat
    )
    ("Hello World" = as text! read %small.dat)
    ("Hello" = read/string/part %small.dat 5)
    ("World" = read/string/seek/part %small.dat 6 5)
    (
        p: open %small.dat
        did all [
            11 = length of p
            "H" = read/string/part p 1
            10 = length of p
            "ello World" = read/string p
            0 = length of p

            "ell" = read/string/seek/part p 1 3
            "o" = as text! read/part p 1
        ]
        elide close p
    )
    (
        p: open %small.dat
        did all [
            (elide skip p 6)
            "World" = read/string p
            null = read/string/part p 100
            (elide skip p -7)
            "o W" = read/string/part p 3
            "orld" = read/string/part p 100
            null = read/string p
        ]
        elide close p
    )
]

; === BLOCK WRITE TESTS ===
[
    (
        write %block.txt ["abc" "def"]
        did all [
            "abcdef" = read/string %block.txt  ; no spaces
            ["abcdef"] = read/lines %block.txt  ; /STRING implicit
            ["abcdef"] = read/string/lines %block.txt
        ]
    )
    (
        write/lines %lines.txt ["abc" "def"]
        did all [
            "abc^/def^/" = read/string %lines.txt
            ["abc" "def"] = read/lines %lines.txt
        ]
    )
    (
        write/part %partial.txt ["foo" "baz" "bar"] 2
        did all [
            "foobaz" = read/string %partial.txt  ; no spaces
            ["foobaz"] = read/lines %partial.txt  ; /STRING implicit
            ["foobaz"] = read/string/lines %partial.txt
        ]
    )
    (
        write/append %partial.txt "bar"
        "foobazbar" = read/string %partial.txt
    )
    (
        p: open %partial.txt
        write p "begins"
        close p
        "beginsbar" = read/string %partial.txt
    )
    (
        p: open %partial.txt
        write/append p "end"
        write p "ing"
        close p
        "beginsbarending" = read/string %partial.txt
    )
]

; === FUZZING WRITE TEST ===
;
; We make an in-memory binary and simulate the writing to the file in parallel
; with actual writing.  The theory would be that when the process is done, we
; wind up with a buffer that's equal to the file contents.
[(
    p: open/new %fuzz.dat

    buffer: copy #{}
    pos: buffer

    fuzzwrite: adapt :write [
      comment [
        print [
            "Writing"
            if part :["part" space :part space "of"], length of data "bytes"
            if seek :["at position" space seek]
        ]
      ]

        if seek [
            pos: skip buffer seek
        ]
        pos: apply :change [pos (copy/part data part) /part part]
    ]

    repeat 128 [
        ; Make a random thing of data up to 1k in size
        ;
        len: random 1024
        data: make binary! len
        repeat len [append data (-1 + random 256)]

        applique :fuzzwrite [
            destination: p
            data: data
            if 4 = random 4 [  ; give it a /PART every 4th write or so
                part: random len
            ]
            all [
                0 != length of buffer  ; RANDOM won't take 0
                4 = random 4  ; same with /SEEK
            ] then [
                seek: (random length of buffer) - 1
            ]
        ]

        ; LENGTH OF should take the position into account for both the file
        ; and for the buffer.
        ;
        assert [(length of p) = (length of pos)]
        assert [(offset of p) = ((index of pos) - 1)]  ; TBD: offset
    ]

    close p

    buffer = read %fuzz.dat
)]

; === DELETE SCRATCH DIRECTORY FROM CURRENT TEST RUN ===
;
; Use the DELETE-DIR instead of the handmade one from the beginning of tests
[
    (change-dir %../, true)
    ('dir = exists? %scratch/)
    (delete-dir %scratch/, true)
    (not exists? %scratch/)
]

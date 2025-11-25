; READ-LINES

[
    (
        test-file: %fixtures/crlf.txt
        write test-file as blob! --[a^Mb^/c^M^/^/d^/^M^/e^/^/f]--
        ok
    )

    ( { READ-LINES }
        lines: collect [
            for-each 'l read-lines test-file [keep l]
        ]
        lines = [{a^Mb} {c} {} {d} {} {e} {} {f}]
    )
    ( { READ-LINES/BINARY }
        lines: collect [
            for-each 'l read-lines/binary test-file [keep l]
        ]
        lines = map-each 'l [{a^Mb} {c} {} {d} {} {e} {} {f}] [as blob! l]
    )
    ( { READ-LINES/DELIMITER }
        eol: "^M^/"
        lines: collect [
            for-each 'l read-lines/delimiter test-file eol [keep l]
        ]
        lines = [{a^Mb^/c} {^/d^/} {e^/^/f}]
    )
    ( { READ-LINES/KEEP }
        lines: collect [
            for-each 'l read-lines/keep test-file [keep l]
        ]
        lines = [{a^Mb^/} {c^M^/} {^/} {d^/} {^M^/} {e^/} {^/} {f}]
    )
    ( { READ-LINES/DELIMITER/KEEP }
        eol: "^M^/"
        lines: collect [
            for-each 'l read-lines/delimiter/keep test-file eol [keep l]
        ]
        lines = [{a^Mb^/c^M^/} {^/d^/^M^/} {e^/^/f}]
    )
]

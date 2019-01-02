; READ-LINES

( { READ-LINES }
    write
    test-file: %fixtures/crlf.txt
    to-binary {a^Mb^/c^M^/^/d^/^M^/e^/^/f}
    lines: mutable make block! 8
    for-each l read-lines test-file [
        append lines l
    ]
    lines = [{a^Mb} {c} {} {d} {} {e} {} {f}]
)
( { READ-LINES/BINARY }
    lines: mutable make block! 8
    for-each l read-lines/binary test-file [
        append lines l
    ]
    lines = map-each l [{a^Mb} {c} {} {d} {} {e} {} {f}] [to-binary l]
)
( { READ-LINES/DELIMITER }
    clear lines
    eol: "^M^/"
    for-each l read-lines/delimiter test-file eol [
        append lines l
    ]
    lines = [{a^Mb^/c} {^/d^/} {e^/^/f}]
)
( { READ-LINES/KEEP }
    clear lines
    for-each l read-lines/keep test-file [
        append lines l
    ]
    lines = [{a^Mb^/} {c^M^/} {^/} {d^/} {^M^/} {e^/} {^/} {f}]
)
( { READ-LINES/DELIMITER/KEEP }
    clear lines
    eol: "^M^/"
    for-each l read-lines/delimiter/keep test-file eol [
        append lines l
    ]
    lines = [{a^Mb^/c^M^/} {^/d^/^M^/} {e^/^/f}]
)

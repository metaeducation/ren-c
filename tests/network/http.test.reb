; Network tests should be part of the extension they are built in.  This
; includes it in the core tests for now.
;
; (While this is a very minimal test, it's better than no tests, which was
; the case in R3-Alpha's test suite.  More should be written, e.g. against
; a known network resource, to check that the returned bytes are actually
; correct.)

[#1613
    ; !!! Note that returning a WORD! from a function ending in ? is not seen
    ; as a good practice, and will likely change.
    ;
    ('file = exists? http://www.rebol.com/index.html)
    (null = exists? http://example.com/fhqwhgads.mp3)
]

(binary? read http://example.com)
(binary? read https://example.com)

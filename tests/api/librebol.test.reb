; !!! Ideally testing libRebol would involve finding the toolchain on a
; system, using the libr3.a or .lib to build executables, and running them
; via CALL...trapping any crashes and making sure the test suite did not
; keep running without noticing.
;
; Until that ideal world materializes, there is a %d-test.c file in the core
; which when INCLUDE_TEST_LIBREBOL_NATIVE is set, will have a TEST-LIBREBOL
; native.  This native returns a block which is a series of integers that
; number the tests (for easy reading) followed by a logic.  Hence if any
; falsey values appear, the test failed--and one can look at the numbers
; to see which one.
;
; If the tests were not built in, an informational string is returned.

(
    result: test-librebol trash
    any [
        text? result  ; informational string

        result =  [
            1 ~true~
            2 ~true~
            3 [1 2 3 d]
            4 [1 2 3 d]
            5 ~true~
        ]
    ]
)

; %static.test.r
;
; STATIC uses a twist on the old method of using self-modifying code to make
; static variables, but allowing arbitrary initialization code, and using
; ALIAS in order to let the static information be any variable...not just a
; BLOCK! of data.
;
; CACHE is a simplified version of the same concept.

(
    runs: 0

    foo: func [num] {
       store: static [runs: runs + 1, 0]
       return store: me + num 
    } 

    all [
       10 = foo 10 
       runs = 1
       30 = foo 20
       runs = 1
    ]
)

(
    runs: 0

    foo: func [num] {
        return num + cache [runs: runs + 1, 100 * 10]
    }

    all [
        1020 = foo 20
        runs = 1
        1304 = foo 304
        runs = 1
    ]
)

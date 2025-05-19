; functions/control/catch.r
; see also functions/control/throw.r
(
    success: ~
    catch [
        throw success: okay
        sucess: null
    ]
    success
)
; catch results
(null? catch [])
(null? catch [()])
(warning? catch [throw trap [1 / 0]])
(1 = catch [throw 1])
((the '~()~) = ^ catch [throw eval ['~()~]])
(warning? first catch [throw reduce [trap [1 / 0]]])
(1 = catch [throw 1])

; recursive cases
(
    num: 1
    catch [
        catch [throw 1]
        num: 2
    ]
    2 = num
)

[#1515 ; the "result" of throw should not be assignable
    (a: 1 catch [a: throw 2] a = 1)
]
(a: 1 catch [set $a throw 2] a = 1)
(a: 1 catch [set:any $a throw 2] a = 1)
[#1509 ; the "result" of throw should not be passable to functions
    (a: 1 catch [a: warning? throw 2] a = 1)
]
[#1535
    (space = catch [words of throw space])
]
(space = catch [values of throw space])
[#1945
    (space = catch [spec of throw space])
]
; throw should not be caught by TRAP
(a: 1 catch [a: warning? trap [throw 2]] a = 1)


; !!! CATCH/NAME is removed for now.  NAME is an argument to CATCH* for the
; WORD! that you want CATCH to use in its body (CATCH is a specialization of
; CATCH* which uses THROW as the name).
;
; It may be that some variation of CATCH allows you to reuse some identity
; that can be used across multiple catches.
;
; (null? catch/name [] 'catch)
; (null? catch/name [()] 'catch)
; (null? catch/name [trap [1 / 0]] 'catch)
; (null? catch/name [1] 'catch)
; ('~()~ = catch/name [throw/name ('~()~) 'catch] 'catch)
; (error? catch/name [throw/name (1 / 0) 'catch] 'catch)
; (1 = catch/name [throw/name 1 'catch] 'catch)
; (
;     num: 1
;     catch [
;         catch/name [
;             throw 1
;         ] 'catch
;         num: 2
;     ]
;     1 = num
; )
; (
;     num: 1
;     catch/name [
;         catch [throw 1]
;         num: 2
;     ] 'catch
;     2 = num
; )
; (
;     num: 1
;     catch/name [
;         catch/name [
;             throw/name 1 'name
;         ] 'name
;         num: 2
;     ] 'name
;     2 = num
; )
; (
;     1 = catch/name [
;         cycling: 'yes
;         while [yes? cycling] [throw/name 1 'a cycling: 'no]
;     ] 'a
; )(
;     cycling: 'yes
;     1 = catch/name [
;         while [if yes? cycling [throw/name 1 'a] 'false] [cycling: 'no]
;     ] 'a
; )
; (1 = catch/name [reduce [throw/name 1 'a]] 'a)
; (a: 1 catch/name [a: throw/name 2 'b] 'b a = 1)
; (a: 1 catch/name [set $a throw/name 2 'b] 'b a = 1)
; (a: 1 catch/name [set:any $a throw/name 2 'b] 'b a = 1)
; (a: 1 catch/name [a: warning? throw/name 2 'b] 'b a = 1)
; (a: 1 catch/name [a: warning? trap [throw/name 2 'b]] 'b a = 1)

; CATCH and RETURN
(
    f: func [return: [integer!]] [catch [return 1] 2]
    1 = f
)
; CATCH and BREAK
(
    null? repeat 1 [
        catch [break 2]
        2
    ]
)

; Multiple return values
(
    all wrap [
        304 = [j b]: catch [throw pack [304 1020]]
        j = 304
        b = 1020
    ]
)
(
    null = catch [10 + 20]
)

; Antiforms
(
    '~#ugly~ = meta catch [throw ~#ugly~]
)

; ELSE/THEN reactivity
[
    (null = catch [throw null])
    (<caught> = catch [throw null] then [<caught>])
    (null = catch [null])
    (null = catch [null] then [panic])
    (<uncaught> = catch [null] else [<uncaught>])
    (<uncaught> = catch [null] then [panic] else [<uncaught>])

    (void? opt catch [throw void])
    (<caught> = catch [throw void] then [<caught>])
    (void? opt catch [void])
    (void? opt catch [void] then [panic])
    (<uncaught> = catch [void] else [<uncaught>])
    (<uncaught> = catch [void] then [panic] else [<uncaught>])
]

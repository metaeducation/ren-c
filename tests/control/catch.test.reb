; functions/control/catch.r
; see also functions/control/throw.r
(
    catch [
        throw success: true
        sucess: false
    ]
    success
)
; catch results
(null? catch [])
(null? catch [()])
(error? catch [throw trap [1 / 0]])
(1 = catch [throw 1])
(''~void~ = ^ catch [throw do ['~void~]])
(error? first catch [throw reduce [trap [1 / 0]]])
(1 = catch [throw 1])
; catch/name results
(null? catch/name [] 'catch)
(null? catch/name [()] 'catch)
(null? catch/name [trap [1 / 0]] 'catch)
(null? catch/name [1] 'catch)
([catch ~void~] = catch/name [throw/name ('~void~) 'catch] 'catch)
(error? first second catch/name [throw/name reduce [trap [1 / 0]] 'catch] 'catch)
([catch 1] = catch/name [throw/name 1 'catch] 'catch)
; recursive cases
(
    num: 1
    catch [
        catch [throw 1]
        num: 2
    ]
    2 = num
)
(
    num: 1
    catch [
        catch/name [
            throw 1
        ] 'catch
        num: 2
    ]
    1 = num
)
(
    num: 1
    catch/name [
        catch [throw 1]
        num: 2
    ] 'catch
    2 = num
)
(
    num: 1
    catch/name [
        catch/name [
            throw/name 1 'name
        ] 'name
        num: 2
    ] 'name
    2 = num
)
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
; CATCH/QUIT
(
    catch/quit [quit]
    true
)
[#851
    (error? trap [catch/quit [] fail make error! ""])
]
[#851
    (null? attempt [catch/quit [] fail make error! ""])
]

; Multiple return values
(
    [c v]: catch [throw 304]
    did all [
        c = 304
        unset? 'v
    ]
)
(
    [c v]: catch [10 + 20]
    did all [
        null? c
        v = 30
    ]
)

; Isotopes
(
    '~ugly~ = ^ catch [throw ~ugly~]
)

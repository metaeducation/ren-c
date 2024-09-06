; functions/control/catch.r
; see also functions/control/throw.r
(
    catch [
        throw success: okay
        sucess: null
    ]
    success
)
; catch results
(null? catch [])
(null? catch [()])
(error? catch [throw trap [1 / 0]])
(1 = catch [throw 1])
((the '~()~) = ^ catch [throw eval ['~()~]])
(error? first catch [throw reduce [trap [1 / 0]]])
(1 = catch [throw 1])

; catch/name results

(null? catch/name [] 'catch)
(null? catch/name [()] 'catch)
(null? catch/name [trap [1 / 0]] 'catch)
(null? catch/name [1] 'catch)

('~()~ = catch/name [throw/name ('~()~) 'catch] 'catch)

(raised? catch/name [throw/name (1 / 0) 'catch] 'catch)

(1 = catch/name [throw/name 1 'catch] 'catch)

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
    ok
)
[#851
    (error? trap [catch/quit [] raise make error! ""])
]
[#851
    (null? attempt [catch/quit [] raise make error! ""])
]

; Multiple return values
(
    all [
        304 = ([j b]: catch [throw pack [304 1020]])
        j = 304
        b = 1020
    ]
)
(
    null = catch [10 + 20]
)

; Antiforms
(
    '~ugly~ = ^ catch [throw ~ugly~]
)

; ELSE/THEN reactivity
[
    (null = catch [throw null])
    (<caught> = catch [throw null] then [<caught>])
    (null = catch [null])
    (null = catch [null] then [fail])
    (<uncaught> = catch [null] else [<uncaught>])
    (<uncaught> = catch [null] then [fail] else [<uncaught>])

    (void? maybe catch [throw void])
    (<caught> = catch [throw void] then [<caught>])
    (void? maybe catch [void])
    (void? maybe catch [void] then [fail])
    (<uncaught> = catch [void] else [<uncaught>])
    (<uncaught> = catch [void] then [fail] else [<uncaught>])
]

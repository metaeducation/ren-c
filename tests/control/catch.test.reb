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
(void? catch [])
(void? catch [()])
(error? catch [trap [1 / 0]])
(1 == catch [1])
(void? catch [throw do []])
(error? first catch [throw reduce [trap [1 / 0]]])
(1 == catch [throw 1])
; catch/name results
(void? catch/name [] 'catch)
(void? catch/name [()] 'catch)
(error? catch/name [trap [1 / 0]] 'catch)
(1 == catch/name [1] 'catch)
(void? catch/name [throw/name (void) 'catch] 'catch)
(error? first catch/name [throw/name reduce [trap [1 / 0]] 'catch] 'catch)
(1 == catch/name [throw/name 1 'catch] 'catch)
; recursive cases
(
    num: 1
    catch [
        catch [throw 1]
        num: 2
    ]
    2 == num
)
(
    num: 1
    catch [
        catch/name [
            throw 1
        ] 'catch
        num: 2
    ]
    1 == num
)
(
    num: 1
    catch/name [
        catch [throw 1]
        num: 2
    ] 'catch
    2 == num
)
(
    num: 1
    catch/name [
        catch/name [
            throw/name 1 'name
        ] 'name
        num: 2
    ] 'name
    2 == num
)
; CATCH and RETURN
(
    f: func [] [catch [return 1] 2]
    1 == f
)
; CATCH and BREAK
(
    null? loop 1 [
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

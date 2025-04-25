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
(error? catch [throw sys/util/rescue [1 / 0]])
(1 = catch [throw 1])
(void? catch [throw eval []])
(1 = catch [throw 1])
; catch/name results
(null? catch/name [] 'catch)
(null? catch/name [()] 'catch)
(null? catch/name [sys/util/rescue [1 / 0]] 'catch)
(null? catch/name [1] 'catch)
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
    f: func [] [catch [return 1] 2]
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
    catch/quit [quit 0]
    okay
)
[#851
    (error? sys/util/rescue [catch/quit [] fail make error! ""])
]

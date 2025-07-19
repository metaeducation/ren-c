; integer!
(1 and+ 1 = 1)
(1 and+ 0 = 0)
(0 and+ 1 = 0)
(0 and+ 0 = 0)
(1 and+ 2 = 0)
(2 and+ 1 = 0)
(2 and+ 2 = 2)

; char!
(#"^(00)" and+ #"^(00)" = #"^(00)")
(#"^(01)" and+ #"^(00)" = #"^(00)")
(#"^(00)" and+ #"^(01)" = #"^(00)")
(#"^(01)" and+ #"^(01)" = #"^(01)")
(#"^(01)" and+ #"^(02)" = #"^(00)")
(#"^(02)" and+ #"^(02)" = #"^(02)")

; tuple!
(0.0.0 and+ 0.0.0 = 0.0.0)
(1.0.0 and+ 1.0.0 = 1.0.0)
(2.0.0 and+ 2.0.0 = 2.0.0)
(255.255.255 and+ 255.255.255 = 255.255.255)

; blob!
(#{030000} and+ #{020000} = #{020000})

; !!! arccosing tests that somehow are in and.test.reb
(0 ?= arccosine 1)
(0 ?= arccosine/radians 1)
(30 ?= arccosine (square-root 3) / 2)
((pi / 6) ?= arccosine/radians (square-root 3) / 2)
(45 ?= arccosine (square-root 2) / 2)
((pi / 4) ?= arccosine/radians (square-root 2) / 2)
(60 ?= arccosine 0.5)
((pi / 3) ?= arccosine/radians 0.5)
(90 ?= arccosine 0)
((pi / 2) ?= arccosine/radians 0)
(180 ?= arccosine -1)
(pi ?= arccosine/radians -1)
(150 ?= arccosine (square-root 3) / -2)
(((pi * 5) / 6) ?= arccosine/radians (square-root 3) / -2)
(135 ?= arccosine (square-root 2) / -2)
(((pi * 3) / 4) ?= arccosine/radians (square-root 2) / -2)
(120 ?= arccosine -0.5)
(((pi * 2) / 3) ?= arccosine/radians -0.5)
(error? sys/util/rescue [arccosine 1.1])
(error? sys/util/rescue [arccosine -1.1])


; If BLOCK! is used for the right clause, it is short circuit.  The first
; falsey value is returned on failure, and the last truthy value on success.
;
(null and null = null)
(null and okay = null)
(okay and null = null)
(okay and okay = okay)
(
    x: 1020
    all [
        okay and (x: null) = null
        x = null
    ]
)
(
    x: null
    all [
        okay and (x: 304) = okay
        x = 304
    ]
)
(
    x: 1020
    all [
        <truthy> and (x: 304) = okay
        x = 304
    ]
)
(
    x: 1020
    all [
        <truthy> and (x: null) = null
        x = null
    ]
)


; If BLOCK! is used for the right clause, it is short circuit.  The first
; falsey value is returned on failure, and the last truthy value on success.
;
(null or null = null)
(null or okay = okay)
(okay or null = okay)
(okay or okay = okay)
(
    x: 1020
    all [
        null or (x: null) = null
        x = null
    ]
)
(
    x: null
    all [
        null or (x: 304) = okay
        x = 304
    ]
)
(
    x: 1020
    all [
        null or (x: okay) = okay
        x = okay
    ]
)

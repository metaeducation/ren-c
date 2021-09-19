; frame.test.reb

(
    foo: function [return: [block!] arg] [
       local: 10
       frame: binding of 'return
       return words of frame
    ]

    did all [
       [arg] = parameters of :foo  ; doesn't expose locals
       [return arg local frame] = foo 20  ; exposes locals as WORD!s
    ]
)


; Names are cached in non-archetypal FRAME! values in order to eventually be
; tunneled out to any ACTION!s or invocations that are produced from them.
(
    f: make frame! :append
    f.series: 1  ; not a valid APPEND target
    f.value: <ae>
    e: trap [do f]
    did all [
        e.id = 'expect-arg
        e.arg1 = 'append
    ]
)

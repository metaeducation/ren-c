; frame.test.reb

(
    foo: function [return: [block!] arg] [
        local: 10
        frame: binding of 'return
        return words of frame
    ]

    all [
        [arg] = words of :foo ;-- doesn't expose locals
        [arg local: frame: return:] = foo 20 ;-- exposes locals as GET-WORD!
    ]
)

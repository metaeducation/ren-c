; RESKINNED is an early concept of a native that rewrites parameter
; conventions.  Would need a bigger plan to be integrated with REDESCRIBE,
; as a general mechanism for updating HELP.  (The native does not mess with
; the HELP, which is structured information that has been pushed out to a
; mostly userspace protocol.)

; Test return type expansion and contraction
(
    returns-int: func [return: [integer!] x] [x]

    returns-text: enclose 'returns-int func [f] [f/x: me + 1 to text! do f]
    returns-text-check: reskinned [return: [integer!]] :returns-text

    skin: reskinned [return: [integer! text!]] :returns-text-check

    did all [
        "11" = returns-text 10
        (trap [returns-text-check 10])/id = 'bad-return-type
        "11" = skin 10
    ]
)

(
    no-decimal-add: reskinned [return: @remove [decimal!]] adapt :add []
    did all [
        10 = no-decimal-add 5 5
        (trap [no-decimal-add 5.0 5.0])/id = 'bad-return-type
    ]
)

; The @add instruction adds an accepted type, leaving the old
(
    foo: func [x [integer!]] [x]
    skin: reskinned [x @add [text!]] (adapt :foo [x: to integer! x])

    did all [
        10 = skin "10"
        10 = skin 10
    ]
)

; No instruction overwrites completely with the new types
(
    foo: func [x [integer!]] [x]
    skin: reskinned [x [text!]] (adapt :foo [x: to integer! x])

    did all [
        10 = skin "10"
        (trap [skin 10])/id = 'expect-arg
    ]
)

; @remove takes away types; doesn't need to be ADAPT-ed or ENCLOSE'd to do so

(
    foo: func [x [integer! text!]] [x]
    skin: reskinned [x @remove [integer!]] :foo

    did all [
        "10" = skin "10"
        (trap [skin 10])/id = 'expect-arg
    ]
)

; You can change the conventions of a function from quoting to non, etc.

(
    skin: reskinned [@change value] :lit
    3 = skin 1 + 2
)

(
    append-lit: reskinned [@change :value] :append
    [a b c d] = append-lit [a b c] d
)

; Ordinarily, when you ADAPT or ENCLOSE a function, the frame filling that is
; done for the adaptation or enclosure does good enough type checking for the
; inner function.  The only parameters it has to worry about rechecking are
; those changed by the code.  Such changes disrupt the ARG_MARKED_CHECKED bit
; that was put on the frame when it was filled.
;
; But if RESKINNED is used to expand the type conventions, then the type
; checking on the original frame filling is no longer trustable.  If it is
; not rechecked on a second pass, then the underlying function will get types
; it did not expect.  If that underlying action is native code, it will crash.
;
; This checks that the type expansion doesn't allow those stale types to
; sneak through unchecked.

(
    skin: reskinned [series @add [integer!]] (adapt :append [])

    e: trap [
        skin 10 "this would crash if there wasn't a recheck"
    ]
    e/id = 'phase-bad-arg-type
)

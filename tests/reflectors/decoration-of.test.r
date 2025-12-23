; %decoration-of.test.r
;
; "Decoration" is a concept subsuming Quoting, Sigils, and leading-blank
; sequence types.

(not decoration of [a b c])
([a b c] = decorate ^ghost [a b c])

(
    d: decoration of the :tu.p.le
    all [
        (the :) = d
        (the :[a b c]) = decorate d [a b c]
    ]
)

(
    d: decoration of the ''@/foobar
    all [
        d = the ''@/
        d: noquote d
        d = the @/
        pinned? d
        d: unpin d
        d = the /

       (the ^/[x y z]) = decorate probe (meta d) [x y z]
    ]
)

((the .) = decoration of the .member)
((the .member.submember) = decorate '. the member.submember)
((the :[any-number? pair!]) = decorate append.dup append.dup.spec)


; UNDECORATE TESTS
;
; Note that composite sequence decorations are ill-defined, not a priority
; so let usage patterns dictate what this should do when people hit it.

((the tu.p.le) = undecorate the :tu.p.le)
((the foo) = undecorate the '''.:foo)

(
    slashed: map-each 'item [@word :tu.p.le] [decorate '/ undecorate item]
    slashed = [/word /tu.p.le]
)

(
    slashed = map-each 'item [@word :tu.p.le] [redecorate '/ item]
    slashed = [/word /tu.p.le]
)

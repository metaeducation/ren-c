; %destructure.test.reb
;
; This is an attempt to implement a dialect that is something like MATCH in
; Rust, or does destructuring the way other languages does.
;
; The concept is you give it a dialected block, where you can define the
; named rules that variables must match.

[
(2 = destructure [1] wrap [
      x: [integer!]
      [x] => [x + 1]
])

(<Case Two> = destructure [a b] [
    [word!] => [fail "Shouldn't call"]
    [word! word!] => [<Case Two>]
])

(
    block: [#stuff 1000 a 20 <tag> #other "items"]

    1020 = destructure block wrap [
        x: [integer!] y: [integer!]

        [... x 'a y ...] => [x + y]
    ]
)

(
    block: [1 2]

    [3 -1] = collect [
        destructure/multi [1 2] wrap [
            x: [integer!] y: [&any-value?]
            m: [&any-value?] n: [integer!]

            [x y] => [keep x + y]
            [m n] => [keep x - y]
        ]
    ]
)
]

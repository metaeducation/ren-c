; %destructure.test.reb
;
; This is an attempt to implement a dialect that is something like MATCH in
; Rust, or does destructuring the way other languages does.
;
; The concept is you give it a dialected block, where you can define the
; named rules that variables must match.

[
(2 = destructure [1] [
      x: [integer!]
      [x] => [x + 1]
])

(<Case Two> = destructure [a b] [
    [word!] => [fail "Shouldn't call"]
    [word! word!] => [<Case Two>]
])

(
    block: [#stuff 1000 a 20 <any> #other "items"]

    1020 = destructure block [
        x: [integer!] y: [integer!]

        [... x 'a y ...] => [x + y]
    ]
)

(
    block: [1 2]

    [3 -1] = collect [
        destructure/multi [1 2] [
            x: [integer!] y: [any-value!]
            m: [any-value!] n: [integer!]

            [x y] => [keep x + y]
            [m n] => [keep x - y]
        ]
    ]
)
]

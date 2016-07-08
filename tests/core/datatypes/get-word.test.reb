; datatypes/get-word.r
[get-word? first [:a]]
[not get-word? 1]
[get-word! = type-of first [:a]]
[
    ; context-less get-word
    e: try [do to block! ":a"]
    e/id = 'not-bound
]
[
    unset 'a
    void? :a
]

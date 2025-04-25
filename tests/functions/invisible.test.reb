; COMMENT is no longer fully invisible (in either bootstrap EXE or mainline)
;
; https://trello.com/c/dWQnsspG

(
    1 = eval [comment "a" 1]
)
(
    void? eval [1 comment "a"]
)
(
    void? eval [comment "a"]
)

; ELIDE is not fully invisible, but trades this off to be able to run its
; code "in turn", instead of being slaved to eager infix evaluation order.
;
; https://trello.com/c/snnG8xwW

(
    1 = eval [elide "a" 1]
)
(
    void? eval [1 elide "a"]
)
(
    void? eval [elide "a"]
)

(
    x: ~
    x: 1 + 2 * 3
    elide (y: :x)

    all [x = 9  y = 9]
)

(
    [3 11] = reduce [1 + 2 elide 3 + 4 5 + 6]
)


(trashified? (if okay [] else [<else>]))
(trashified? (if okay [comment <true-branch>] else [<else>]))

(1 = all [1 elide <invisible>])
(1 = any [1 elide <invisible>])
([1] = reduce [1 elide <invisible>])


(void? reeval the (comment "void is better than failing here"))
(
    x: <before>
    all [
        void? reeval :elide x: <after>
        x = <after>
    ]
)


; !!! Tests of invisibles interacting with functions should be in the file
; where those functions are defined, when test file structure gets improved.
;
(null? spaced [])
(null? spaced [comment "hi"])
(null? spaced [()])

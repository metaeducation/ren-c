; functions/context/unset.r
(
    a: ~
    not set? 'a
)
(
    set 'a ~
    not set? 'a
)
(
    a: ~
    unset? 'a
)
(
    set 'a ~
    unset? 'a
)
(
    a: 10
    unset 'a
    not set? 'a
)

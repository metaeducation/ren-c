; functions/context/unset.r
(
    a: <something>
    unset 'a
    not set? 'a
)
(
    a: <something>
    unset 'a
    unset 'a
    not set? 'a
)

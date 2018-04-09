; functions/context/unset.r
(
    a: _
    unset 'a
    not set? 'a
)
(
    a: _
    unset 'a
    unset 'a
    not set? 'a
)

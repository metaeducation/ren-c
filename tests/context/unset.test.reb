; functions/context/unset.r
(
    a: null
    not set? 'a
)
(
    set 'a null
    not set? 'a
)
(
    a: void
    voided? 'a
)
(
    set 'a void
    voided? 'a
)
(
    success: <bad>
    if 1 > 2 [success: null] else [success: okay]
    success
)
(
    success: <bad>
    if 1 < 2 [success: okay] else [success: null]
    success
)
(
    success: <bad>
    if-not 1 > 2 [success: okay] else [success: null]
    success
)
(
    success: <bad>
    if-not 1 < 2 [success: null] else [success: okay]
    success
)
(
    success: <bad>
    if okay does [success: okay]
    success
)
(
    success: okay
    if null does [success: null]
    success
)

(
    ; https://github.com/metaeducation/ren-c/issues/510
    ;
    ; !!! RETURN has been changed to being <end>-able, where endability is
    ; currently the very hack by which <- assumes you want "to the end".
    ; It's curious that RETURN would turn out to purposefully need the very
    ; feature that was chosen as the "hack" to signal overriding eager
    ; left completion...which may indicate the hack has merit.  The issue
    ; is being left open to explore, but for the moment the quirk is "fixed"
    ; for the case of RETURN.

    c: func [i] [
        return if i < 15 [30] else [4]
    ]

    d: func [i] [
        return <- if i < 15 [30] else [4]
    ]

    all [
        30 = c 10
        4 = c 20 ;-- !!! was () = c 20
        30 = d 10
        4 = d 20
    ]
)

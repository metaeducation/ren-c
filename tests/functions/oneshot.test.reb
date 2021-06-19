; oneshot (n-shot, upshot...)

(
    once: oneshot
    did all [
        10 = once [5 + 5]
        null = once [5 + 5]
        null = once [5 + 5]
    ]
)(
    up: upshot
    did all [
        null = up [5 + 5]
        10 = up [5 + 5]
        10 = up [5 + 5]
    ]
)(
    once: oneshot
    '~null~ = ^ once [null]
)

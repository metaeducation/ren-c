; better-than-nothing (New)APPLY tests

(
    s: applique :append [series: [a b c] value: [d e] dup: okay count: 2]
    s = [a b c d e d e]
)

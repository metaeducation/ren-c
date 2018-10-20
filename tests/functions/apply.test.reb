; better-than-nothing (New)APPLY tests

(
    s: apply :append [series: [a b c] value: [d e] dup: true count: 2]
    s == [a b c d e d e]
)

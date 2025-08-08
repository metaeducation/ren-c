; %bar-bar-bar.test.r
;
; Evaluation that discards everything afterward without running it.
; (variadically spools values inertly)

(
    void? eval [|||]
)
(
    3 = eval [1 + 2 ||| 10 + 20, 100 + 200]
)
(
    x: 1020
    y: 304
    all [
        3 = eval [1 + 2 ||| x: 0, y: 0]
        x = 1020
        y = 304
    ]
)

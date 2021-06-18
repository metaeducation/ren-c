; functions/context/valueq.r
(false == set? 'nonsense)
(true == set? 'set?)

[#1914 (
    set? reeval func [x] ['x] blank
)]

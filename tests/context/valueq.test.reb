; functions/context/valueq.r
(false == set? 'nonsense)
(true == set? 'set?)
; #1914 ... Ren-C indefinite extent prioritizes failure if not indefinite
(error? trap [set? reeval func [x] ['x] blank])

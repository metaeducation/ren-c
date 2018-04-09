; functions/context/valueq.r
(false == set? 'nonsense)
(true == set? 'set?)
; #1914 ... Ren-C indefinite extent prioritizes failure if not indefinite
(error? try [set? eval func [x] ['x] blank])

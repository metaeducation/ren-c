; functions/context/valueq.r
(true == unset? 'nonsense)
(true == not unset? 'unset?)
; #1914 ... Ren-C indefinite extent prioritizes failure if not indefinite
(error? trap [set? reeval func [x] ['x] blank])

; functions/context/valueq.r
(okay = unset? 'nonsense)
(okay = not unset? 'unset?)
; #1914 ... Ren-C indefinite extent prioritizes failure if not indefinite
(error? sys/util/rescue [set? reeval func [x] ['x] blank])

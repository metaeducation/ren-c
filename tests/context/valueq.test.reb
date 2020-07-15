; functions/context/valueq.r
(true == voided? 'nonsense)
(true == not voided? 'voided?)
; #1914 ... Ren-C indefinite extent prioritizes failure if not indefinite
(error? trap [set? reeval func [x] ['x] blank])

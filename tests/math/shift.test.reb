; functions/math/shift.r
; logical shift of decode [BE +/-] #{8000000000000000}
<64bit>
[#2067
    (strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000000} decode [BE +/-] #{8000000000000000})
]
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000000} decode [BE +/-] #{8000000000000001})
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000000} -65)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000000} -64)
<64bit>
(strict-equal? 1 shift:logical decode [BE +/-] #{8000000000000000} -63)
<64bit>
(strict-equal? 2 shift:logical decode [BE +/-] #{8000000000000000} -62)
<64bit>
(strict-equal? decode [BE +/-] #{4000000000000000} shift:logical decode [BE +/-] #{8000000000000000} -1)
<64bit>
(strict-equal? decode [BE +/-] #{8000000000000000} shift:logical decode [BE +/-] #{8000000000000000} 0)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000000} 1)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000000} 62)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000000} 63)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000000} 64)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000000} 65)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000000} decode [BE +/-] #{7ffffffffffffffe})
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000000} decode [BE +/-] #{7fffffffffffffff})
; logical shift of decode [BE +/-] #{8000000000000001}
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000001} decode [BE +/-] #{8000000000000000})
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000001} decode [BE +/-] #{8000000000000001})
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000001} -65)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000001} -64)
<64bit>
(strict-equal? 1 shift:logical decode [BE +/-] #{8000000000000001} -63)
<64bit>
(strict-equal? 2 shift:logical decode [BE +/-] #{8000000000000001} -62)
<64bit>
(strict-equal? decode [BE +/-] #{4000000000000000} shift:logical decode [BE +/-] #{8000000000000001} -1)
<64bit>
(strict-equal? decode [BE +/-] #{8000000000000001} shift:logical decode [BE +/-] #{8000000000000001} 0)
<64bit>
(strict-equal? 2 shift:logical decode [BE +/-] #{8000000000000001} 1)
<64bit>
(strict-equal? decode [BE +/-] #{4000000000000000} shift:logical decode [BE +/-] #{8000000000000001} 62)
<64bit>
(strict-equal? decode [BE +/-] #{8000000000000000} shift:logical decode [BE +/-] #{8000000000000001} 63)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000001} 64)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000001} 65)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000001} decode [BE +/-] #{7ffffffffffffffe})
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{8000000000000001} decode [BE +/-] #{7fffffffffffffff})
; logical shift of -1
<64bit>
(strict-equal? 0 shift:logical -1 decode [BE +/-] #{8000000000000000})
<64bit>
(strict-equal? 0 shift:logical -1 decode [BE +/-] #{8000000000000001})
(strict-equal? 0 shift:logical -1 -65)
(strict-equal? 0 shift:logical -1 -64)
<64bit>
(strict-equal? 1 shift:logical -1 -63)
(strict-equal? 3 shift:logical -1 -62)
<64bit>
(strict-equal? decode [BE +/-] #{7fffffffffffffff} shift:logical -1 -1)
(strict-equal? -1 shift:logical -1 0)
(strict-equal? -2 shift:logical -1 1)
<64bit>
(strict-equal? decode [BE +/-] #{c000000000000000} shift:logical -1 62)
<64bit>
(strict-equal? decode [BE +/-] #{8000000000000000} shift:logical -1 63)
(strict-equal? 0 shift:logical -1 64)
(strict-equal? 0 shift:logical -1 65)
<64bit>
(strict-equal? 0 shift:logical -1 decode [BE +/-] #{7ffffffffffffffe})
<64bit>
(strict-equal? 0 shift:logical -1 decode [BE +/-] #{7fffffffffffffff})
; logical shift of 0
<64bit>
(strict-equal? 0 shift:logical 0 decode [BE +/-] #{8000000000000000})
<64bit>
(strict-equal? 0 shift:logical 0 decode [BE +/-] #{8000000000000001})
(strict-equal? 0 shift:logical 0 -65)
(strict-equal? 0 shift:logical 0 -64)
(strict-equal? 0 shift:logical 0 -63)
(strict-equal? 0 shift:logical 0 -62)
(strict-equal? 0 shift:logical 0 -1)
(strict-equal? 0 shift:logical 0 0)
(strict-equal? 0 shift:logical 0 1)
(strict-equal? 0 shift:logical 0 62)
(strict-equal? 0 shift:logical 0 63)
(strict-equal? 0 shift:logical 0 64)
(strict-equal? 0 shift:logical 0 65)
<64bit>
(strict-equal? 0 shift:logical 0 decode [BE +/-] #{7ffffffffffffffe})
<64bit>
(strict-equal? 0 shift:logical 0 decode [BE +/-] #{7fffffffffffffff})
; logical shift of 1
<64bit>
(strict-equal? 0 shift:logical 1 decode [BE +/-] #{8000000000000000})
<64bit>
(strict-equal? 0 shift:logical 1 decode [BE +/-] #{8000000000000001})
(strict-equal? 0 shift:logical 1 -65)
(strict-equal? 0 shift:logical 1 -64)
(strict-equal? 0 shift:logical 1 -63)
(strict-equal? 0 shift:logical 1 -62)
(strict-equal? 0 shift:logical 1 -1)
(strict-equal? 1 shift:logical 1 0)
(strict-equal? 2 shift:logical 1 1)
<64bit>
(strict-equal? decode [BE +/-] #{4000000000000000} shift:logical 1 62)
<64bit>
(strict-equal? decode [BE +/-] #{8000000000000000} shift:logical 1 63)
(strict-equal? 0 shift:logical 1 64)
(strict-equal? 0 shift:logical 1 65)
<64bit>
(strict-equal? 0 shift:logical 1 decode [BE +/-] #{7ffffffffffffffe})
<64bit>
(strict-equal? 0 shift:logical 1 decode [BE +/-] #{7fffffffffffffff})
; logical shift of decode [BE +/-] #{7ffffffffffffffe}
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7ffffffffffffffe} decode [BE +/-] #{8000000000000000})
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7ffffffffffffffe} decode [BE +/-] #{8000000000000001})
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7ffffffffffffffe} -65)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7ffffffffffffffe} -64)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7ffffffffffffffe} -63)
<64bit>
(strict-equal? 1 shift:logical decode [BE +/-] #{7ffffffffffffffe} -62)
<64bit>
(strict-equal? decode [BE +/-] #{3fffffffffffffff} shift:logical decode [BE +/-] #{7ffffffffffffffe} -1)
<64bit>
(strict-equal? decode [BE +/-] #{7ffffffffffffffe} shift:logical decode [BE +/-] #{7ffffffffffffffe} 0)
<64bit>
(strict-equal? -4 shift:logical decode [BE +/-] #{7ffffffffffffffe} 1)
<64bit>
(strict-equal? decode [BE +/-] #{8000000000000000} shift:logical decode [BE +/-] #{7ffffffffffffffe} 62)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7ffffffffffffffe} 63)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7ffffffffffffffe} 64)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7ffffffffffffffe} 65)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7ffffffffffffffe} decode [BE +/-] #{7ffffffffffffffe})
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7ffffffffffffffe} decode [BE +/-] #{7fffffffffffffff})
; logical shift of decode [BE +/-] #{7fffffffffffffff}
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7fffffffffffffff} decode [BE +/-] #{8000000000000000})
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7fffffffffffffff} decode [BE +/-] #{8000000000000001})
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7fffffffffffffff} -65)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7fffffffffffffff} -64)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7fffffffffffffff} -63)
<64bit>
(strict-equal? 1 shift:logical decode [BE +/-] #{7fffffffffffffff} -62)
<64bit>
(strict-equal? decode [BE +/-] #{3fffffffffffffff} shift:logical decode [BE +/-] #{7fffffffffffffff} -1)
<64bit>
(strict-equal? decode [BE +/-] #{7fffffffffffffff} shift:logical decode [BE +/-] #{7fffffffffffffff} 0)
<64bit>
(strict-equal? -2 shift:logical decode [BE +/-] #{7fffffffffffffff} 1)
<64bit>
(strict-equal? decode [BE +/-] #{c000000000000000} shift:logical decode [BE +/-] #{7fffffffffffffff} 62)
<64bit>
(strict-equal? decode [BE +/-] #{8000000000000000} shift:logical decode [BE +/-] #{7fffffffffffffff} 63)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7fffffffffffffff} 64)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7fffffffffffffff} 65)
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7fffffffffffffff} decode [BE +/-] #{7ffffffffffffffe})
<64bit>
(strict-equal? 0 shift:logical decode [BE +/-] #{7fffffffffffffff} decode [BE +/-] #{7fffffffffffffff})
; arithmetic shift of decode [BE +/-] #{8000000000000000}
<64bit>
(strict-equal? -1 shift decode [BE +/-] #{8000000000000000} decode [BE +/-] #{8000000000000000})
<64bit>
(strict-equal? -1 shift decode [BE +/-] #{8000000000000000} decode [BE +/-] #{8000000000000001})
<64bit>
(strict-equal? -1 shift decode [BE +/-] #{8000000000000000} -65)
<64bit>
(strict-equal? -1 shift decode [BE +/-] #{8000000000000000} -64)
<64bit>
(strict-equal? -1 shift decode [BE +/-] #{8000000000000000} -63)
<64bit>
(strict-equal? -2 shift decode [BE +/-] #{8000000000000000} -62)
<64bit>
(strict-equal? decode [BE +/-] #{c000000000000000} shift decode [BE +/-] #{8000000000000000} -1)
<64bit>
(strict-equal? decode [BE +/-] #{8000000000000000} shift decode [BE +/-] #{8000000000000000} 0)

<64bit> ~overflow~ !! (shift decode [BE +/-] #{8000000000000000} 1)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{8000000000000000} 62)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{8000000000000000} 63)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{8000000000000000} 64)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{8000000000000000} 65)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{8000000000000000} decode [BE +/-] #{7ffffffffffffffe})
<64bit> ~overflow~ !! (shift decode [BE +/-] #{8000000000000000} decode [BE +/-] #{7fffffffffffffff})

; arithmetic shift of decode [BE +/-] #{8000000000000001}

<64bit>
(strict-equal? -1 shift decode [BE +/-] #{8000000000000001} decode [BE +/-] #{8000000000000000})
<64bit>
(strict-equal? -1 shift decode [BE +/-] #{8000000000000001} decode [BE +/-] #{8000000000000001})
<64bit>
(strict-equal? -1 shift decode [BE +/-] #{8000000000000001} -65)
<64bit>
(strict-equal? -1 shift decode [BE +/-] #{8000000000000001} -64)
<64bit>
(strict-equal? -1 shift decode [BE +/-] #{8000000000000001} -63)
<64bit>
(strict-equal? -2 shift decode [BE +/-] #{8000000000000001} -62)
<64bit>
(strict-equal? decode [BE +/-] #{c000000000000000} shift decode [BE +/-] #{8000000000000001} -1)
<64bit>
(strict-equal? decode [BE +/-] #{8000000000000001} shift decode [BE +/-] #{8000000000000001} 0)

<64bit> ~overflow~ !! (shift decode [BE +/-] #{8000000000000001} 1)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{8000000000000001} 62)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{8000000000000001} 63)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{8000000000000001} 64)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{8000000000000001} 65)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{8000000000000001} decode [BE +/-] #{7ffffffffffffffe})
<64bit> ~overflow~ !! (shift decode [BE +/-] #{8000000000000001} decode [BE +/-] #{7fffffffffffffff})

; arithmetic shift of -1
<64bit>
(strict-equal? -1 shift -1 decode [BE +/-] #{8000000000000000})
<64bit>
(strict-equal? -1 shift -1 decode [BE +/-] #{8000000000000001})
(strict-equal? -1 shift -1 -65)
(strict-equal? -1 shift -1 -64)
(strict-equal? -1 shift -1 -63)
(strict-equal? -1 shift -1 -62)
(strict-equal? -1 shift -1 -1)
(strict-equal? -1 shift -1 0)
(strict-equal? -2 shift -1 1)
<64bit>
(strict-equal? decode [BE +/-] #{c000000000000000} shift -1 62)
<64bit>
(strict-equal? decode [BE +/-] #{8000000000000000} shift -1 63)

~overflow~ !! (shift -1 64)
~overflow~ !! (shift -1 65)

<64bit> ~overflow~ !! (shift -1 decode [BE +/-] #{7ffffffffffffffe})
<64bit> ~overflow~ !! (shift -1 decode [BE +/-] #{7fffffffffffffff})

; arithmetic shift of 0
<64bit>
(strict-equal? 0 shift 0 decode [BE +/-] #{8000000000000000})
<64bit>
(strict-equal? 0 shift 0 decode [BE +/-] #{8000000000000001})
(strict-equal? 0 shift 0 -65)
(strict-equal? 0 shift 0 -64)
(strict-equal? 0 shift 0 -63)
(strict-equal? 0 shift 0 -62)
(strict-equal? 0 shift 0 -1)
(strict-equal? 0 shift 0 0)
(strict-equal? 0 shift 0 1)
(strict-equal? 0 shift 0 62)
(strict-equal? 0 shift 0 63)
(strict-equal? 0 shift 0 64)
(strict-equal? 0 shift 0 65)
<64bit>
(strict-equal? 0 shift 0 decode [BE +/-] #{7ffffffffffffffe})
<64bit>
(strict-equal? 0 shift 0 decode [BE +/-] #{7fffffffffffffff})
; arithmetic shift of 1
<64bit>
(strict-equal? 0 shift 1 decode [BE +/-] #{8000000000000000})
<64bit>
(strict-equal? 0 shift 1 decode [BE +/-] #{8000000000000001})
(strict-equal? 0 shift 1 -65)
(strict-equal? 0 shift 1 -64)
(strict-equal? 0 shift 1 -63)
(strict-equal? 0 shift 1 -62)
(strict-equal? 0 shift 1 -1)
(strict-equal? 1 shift 1 0)
(strict-equal? 2 shift 1 1)
<64bit>
(strict-equal? decode [BE +/-] #{4000000000000000} shift 1 62)

~overflow~ !! (shift 1 63)
~overflow~ !! (shift 1 64)
~overflow~ !! (shift 1 65)

<64bit> ~overflow~ !! (shift 1 decode [BE +/-] #{7ffffffffffffffe})
<64bit> ~overflow~ !! (shift 1 decode [BE +/-] #{7fffffffffffffff})

; arithmetic shift of decode [BE +/-] #{7ffffffffffffffe}
<64bit>
(strict-equal? 0 shift decode [BE +/-] #{7ffffffffffffffe} decode [BE +/-] #{8000000000000000})
<64bit>
(strict-equal? 0 shift decode [BE +/-] #{7ffffffffffffffe} decode [BE +/-] #{8000000000000001})
<64bit>
(strict-equal? 0 shift decode [BE +/-] #{7ffffffffffffffe} -65)
<64bit>
(strict-equal? 0 shift decode [BE +/-] #{7ffffffffffffffe} -64)
<64bit>
(strict-equal? 0 shift decode [BE +/-] #{7ffffffffffffffe} -63)
<64bit>
(strict-equal? 1 shift decode [BE +/-] #{7ffffffffffffffe} -62)
<64bit>
(strict-equal? decode [BE +/-] #{3fffffffffffffff} shift decode [BE +/-] #{7ffffffffffffffe} -1)
<64bit>
(strict-equal? decode [BE +/-] #{7ffffffffffffffe} shift decode [BE +/-] #{7ffffffffffffffe} 0)

<64bit> ~overflow~ !! (shift decode [BE +/-] #{7ffffffffffffffe} 1)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{7ffffffffffffffe} 62)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{7ffffffffffffffe} 63)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{7ffffffffffffffe} 64)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{7ffffffffffffffe} 65)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{7ffffffffffffffe} decode [BE +/-] #{7ffffffffffffffe})
<64bit> ~overflow~ !! (shift decode [BE +/-] #{7ffffffffffffffe} decode [BE +/-] #{7fffffffffffffff})

; arithmetic shift of decode [BE +/-] #{7fffffffffffffff}
<64bit>
(strict-equal? 0 shift decode [BE +/-] #{7fffffffffffffff} decode [BE +/-] #{8000000000000000})
<64bit>
(strict-equal? 0 shift decode [BE +/-] #{7fffffffffffffff} decode [BE +/-] #{8000000000000001})
<64bit>
(strict-equal? 0 shift decode [BE +/-] #{7fffffffffffffff} -65)
<64bit>
(strict-equal? 0 shift decode [BE +/-] #{7fffffffffffffff} -64)
<64bit>
(strict-equal? 0 shift decode [BE +/-] #{7fffffffffffffff} -63)
<64bit>
(strict-equal? 1 shift decode [BE +/-] #{7fffffffffffffff} -62)
<64bit>
(strict-equal? decode [BE +/-] #{3fffffffffffffff} shift decode [BE +/-] #{7fffffffffffffff} -1)
<64bit>
(strict-equal? decode [BE +/-] #{7fffffffffffffff} shift decode [BE +/-] #{7fffffffffffffff} 0)

<64bit> ~overflow~ !! (shift decode [BE +/-] #{7fffffffffffffff} 1)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{7fffffffffffffff} 62)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{7fffffffffffffff} 63)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{7fffffffffffffff} 64)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{7fffffffffffffff} 65)
<64bit> ~overflow~ !! (shift decode [BE +/-] #{7fffffffffffffff} decode [BE +/-] #{7ffffffffffffffe})
<64bit> ~overflow~ !! (shift decode [BE +/-] #{7fffffffffffffff} decode [BE +/-] #{7fffffffffffffff})

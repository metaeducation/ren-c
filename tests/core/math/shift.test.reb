; functions/math/shift.r
; bug#2067
; logical shift of to integer! #{8000000000000000}
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000000} to integer! #{8000000000000000}]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000000} to integer! #{8000000000000001}]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000000} -65]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000000} -64]
#64bit
[strict-equal? 1 shift/logical to integer! #{8000000000000000} -63]
#64bit
[strict-equal? 2 shift/logical to integer! #{8000000000000000} -62]
#64bit
[strict-equal? to integer! #{4000000000000000} shift/logical to integer! #{8000000000000000} -1]
#64bit
[strict-equal? to integer! #{8000000000000000} shift/logical to integer! #{8000000000000000} 0]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000000} 1]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000000} 62]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000000} 63]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000000} 64]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000000} 65]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000000} to integer! #{7ffffffffffffffe}]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000000} to integer! #{7fffffffffffffff}]
; logical shift of to integer! #{8000000000000001}
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000001} to integer! #{8000000000000000}]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000001} to integer! #{8000000000000001}]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000001} -65]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000001} -64]
#64bit
[strict-equal? 1 shift/logical to integer! #{8000000000000001} -63]
#64bit
[strict-equal? 2 shift/logical to integer! #{8000000000000001} -62]
#64bit
[strict-equal? to integer! #{4000000000000000} shift/logical to integer! #{8000000000000001} -1]
#64bit
[strict-equal? to integer! #{8000000000000001} shift/logical to integer! #{8000000000000001} 0]
#64bit
[strict-equal? 2 shift/logical to integer! #{8000000000000001} 1]
#64bit
[strict-equal? to integer! #{4000000000000000} shift/logical to integer! #{8000000000000001} 62]
#64bit
[strict-equal? to integer! #{8000000000000000} shift/logical to integer! #{8000000000000001} 63]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000001} 64]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000001} 65]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000001} to integer! #{7ffffffffffffffe}]
#64bit
[strict-equal? 0 shift/logical to integer! #{8000000000000001} to integer! #{7fffffffffffffff}]
; logical shift of -1
#64bit
[strict-equal? 0 shift/logical -1 to integer! #{8000000000000000}]
#64bit
[strict-equal? 0 shift/logical -1 to integer! #{8000000000000001}]
[strict-equal? 0 shift/logical -1 -65]
[strict-equal? 0 shift/logical -1 -64]
#64bit
[strict-equal? 1 shift/logical -1 -63]
[strict-equal? 3 shift/logical -1 -62]
#64bit
[strict-equal? to integer! #{7fffffffffffffff} shift/logical -1 -1]
[strict-equal? -1 shift/logical -1 0]
[strict-equal? -2 shift/logical -1 1]
#64bit
[strict-equal? to integer! #{c000000000000000} shift/logical -1 62]
#64bit
[strict-equal? to integer! #{8000000000000000} shift/logical -1 63]
[strict-equal? 0 shift/logical -1 64]
[strict-equal? 0 shift/logical -1 65]
#64bit
[strict-equal? 0 shift/logical -1 to integer! #{7ffffffffffffffe}]
#64bit
[strict-equal? 0 shift/logical -1 to integer! #{7fffffffffffffff}]
; logical shift of 0
#64bit
[strict-equal? 0 shift/logical 0 to integer! #{8000000000000000}]
#64bit
[strict-equal? 0 shift/logical 0 to integer! #{8000000000000001}]
[strict-equal? 0 shift/logical 0 -65]
[strict-equal? 0 shift/logical 0 -64]
[strict-equal? 0 shift/logical 0 -63]
[strict-equal? 0 shift/logical 0 -62]
[strict-equal? 0 shift/logical 0 -1]
[strict-equal? 0 shift/logical 0 0]
[strict-equal? 0 shift/logical 0 1]
[strict-equal? 0 shift/logical 0 62]
[strict-equal? 0 shift/logical 0 63]
[strict-equal? 0 shift/logical 0 64]
[strict-equal? 0 shift/logical 0 65]
#64bit
[strict-equal? 0 shift/logical 0 to integer! #{7ffffffffffffffe}]
#64bit
[strict-equal? 0 shift/logical 0 to integer! #{7fffffffffffffff}]
; logical shift of 1
#64bit
[strict-equal? 0 shift/logical 1 to integer! #{8000000000000000}]
#64bit
[strict-equal? 0 shift/logical 1 to integer! #{8000000000000001}]
[strict-equal? 0 shift/logical 1 -65]
[strict-equal? 0 shift/logical 1 -64]
[strict-equal? 0 shift/logical 1 -63]
[strict-equal? 0 shift/logical 1 -62]
[strict-equal? 0 shift/logical 1 -1]
[strict-equal? 1 shift/logical 1 0]
[strict-equal? 2 shift/logical 1 1]
#64bit
[strict-equal? to integer! #{4000000000000000} shift/logical 1 62]
#64bit
[strict-equal? to integer! #{8000000000000000} shift/logical 1 63]
[strict-equal? 0 shift/logical 1 64]
[strict-equal? 0 shift/logical 1 65]
#64bit
[strict-equal? 0 shift/logical 1 to integer! #{7ffffffffffffffe}]
#64bit
[strict-equal? 0 shift/logical 1 to integer! #{7fffffffffffffff}]
; logical shift of to integer! #{7ffffffffffffffe}
#64bit
[strict-equal? 0 shift/logical to integer! #{7ffffffffffffffe} to integer! #{8000000000000000}]
#64bit
[strict-equal? 0 shift/logical to integer! #{7ffffffffffffffe} to integer! #{8000000000000001}]
#64bit
[strict-equal? 0 shift/logical to integer! #{7ffffffffffffffe} -65]
#64bit
[strict-equal? 0 shift/logical to integer! #{7ffffffffffffffe} -64]
#64bit
[strict-equal? 0 shift/logical to integer! #{7ffffffffffffffe} -63]
#64bit
[strict-equal? 1 shift/logical to integer! #{7ffffffffffffffe} -62]
#64bit
[strict-equal? to integer! #{3fffffffffffffff} shift/logical to integer! #{7ffffffffffffffe} -1]
#64bit
[strict-equal? to integer! #{7ffffffffffffffe} shift/logical to integer! #{7ffffffffffffffe} 0]
#64bit
[strict-equal? -4 shift/logical to integer! #{7ffffffffffffffe} 1]
#64bit
[strict-equal? to integer! #{8000000000000000} shift/logical to integer! #{7ffffffffffffffe} 62]
#64bit
[strict-equal? 0 shift/logical to integer! #{7ffffffffffffffe} 63]
#64bit
[strict-equal? 0 shift/logical to integer! #{7ffffffffffffffe} 64]
#64bit
[strict-equal? 0 shift/logical to integer! #{7ffffffffffffffe} 65]
#64bit
[strict-equal? 0 shift/logical to integer! #{7ffffffffffffffe} to integer! #{7ffffffffffffffe}]
#64bit
[strict-equal? 0 shift/logical to integer! #{7ffffffffffffffe} to integer! #{7fffffffffffffff}]
; logical shift of to integer! #{7fffffffffffffff}
#64bit
[strict-equal? 0 shift/logical to integer! #{7fffffffffffffff} to integer! #{8000000000000000}]
#64bit
[strict-equal? 0 shift/logical to integer! #{7fffffffffffffff} to integer! #{8000000000000001}]
#64bit
[strict-equal? 0 shift/logical to integer! #{7fffffffffffffff} -65]
#64bit
[strict-equal? 0 shift/logical to integer! #{7fffffffffffffff} -64]
#64bit
[strict-equal? 0 shift/logical to integer! #{7fffffffffffffff} -63]
#64bit
[strict-equal? 1 shift/logical to integer! #{7fffffffffffffff} -62]
#64bit
[strict-equal? to integer! #{3fffffffffffffff} shift/logical to integer! #{7fffffffffffffff} -1]
#64bit
[strict-equal? to integer! #{7fffffffffffffff} shift/logical to integer! #{7fffffffffffffff} 0]
#64bit
[strict-equal? -2 shift/logical to integer! #{7fffffffffffffff} 1]
#64bit
[strict-equal? to integer! #{c000000000000000} shift/logical to integer! #{7fffffffffffffff} 62]
#64bit
[strict-equal? to integer! #{8000000000000000} shift/logical to integer! #{7fffffffffffffff} 63]
#64bit
[strict-equal? 0 shift/logical to integer! #{7fffffffffffffff} 64]
#64bit
[strict-equal? 0 shift/logical to integer! #{7fffffffffffffff} 65]
#64bit
[strict-equal? 0 shift/logical to integer! #{7fffffffffffffff} to integer! #{7ffffffffffffffe}]
#64bit
[strict-equal? 0 shift/logical to integer! #{7fffffffffffffff} to integer! #{7fffffffffffffff}]
; arithmetic shift of to integer! #{8000000000000000}
#64bit
[strict-equal? -1 shift to integer! #{8000000000000000} to integer! #{8000000000000000}]
#64bit
[strict-equal? -1 shift to integer! #{8000000000000000} to integer! #{8000000000000001}]
#64bit
[strict-equal? -1 shift to integer! #{8000000000000000} -65]
#64bit
[strict-equal? -1 shift to integer! #{8000000000000000} -64]
#64bit
[strict-equal? -1 shift to integer! #{8000000000000000} -63]
#64bit
[strict-equal? -2 shift to integer! #{8000000000000000} -62]
#64bit
[strict-equal? to integer! #{c000000000000000} shift to integer! #{8000000000000000} -1]
#64bit
[strict-equal? to integer! #{8000000000000000} shift to integer! #{8000000000000000} 0]
#64bit
[error? try [shift to integer! #{8000000000000000} 1]]
#64bit
[error? try [shift to integer! #{8000000000000000} 62]]
#64bit
[error? try [shift to integer! #{8000000000000000} 63]]
#64bit
[error? try [shift to integer! #{8000000000000000} 64]]
#64bit
[error? try [shift to integer! #{8000000000000000} 65]]
#64bit
[error? try [shift to integer! #{8000000000000000} to integer! #{7ffffffffffffffe}]]
#64bit
[error? try [shift to integer! #{8000000000000000} to integer! #{7fffffffffffffff}]]
#64bit
; arithmetic shift of to integer! #{8000000000000001}
#64bit
[strict-equal? -1 shift to integer! #{8000000000000001} to integer! #{8000000000000000}]
#64bit
[strict-equal? -1 shift to integer! #{8000000000000001} to integer! #{8000000000000001}]
#64bit
[strict-equal? -1 shift to integer! #{8000000000000001} -65]
#64bit
[strict-equal? -1 shift to integer! #{8000000000000001} -64]
#64bit
[strict-equal? -1 shift to integer! #{8000000000000001} -63]
#64bit
[strict-equal? -2 shift to integer! #{8000000000000001} -62]
#64bit
[strict-equal? to integer! #{c000000000000000} shift to integer! #{8000000000000001} -1]
#64bit
[strict-equal? to integer! #{8000000000000001} shift to integer! #{8000000000000001} 0]
#64bit
[error? try [shift to integer! #{8000000000000001} 1]]
#64bit
[error? try [shift to integer! #{8000000000000001} 62]]
#64bit
[error? try [shift to integer! #{8000000000000001} 63]]
#64bit
[error? try [shift to integer! #{8000000000000001} 64]]
#64bit
[error? try [shift to integer! #{8000000000000001} 65]]
#64bit
[error? try [shift to integer! #{8000000000000001} to integer! #{7ffffffffffffffe}]]
#64bit
[error? try [shift to integer! #{8000000000000001} to integer! #{7fffffffffffffff}]]
; arithmetic shift of -1
#64bit
[strict-equal? -1 shift -1 to integer! #{8000000000000000}]
#64bit
[strict-equal? -1 shift -1 to integer! #{8000000000000001}]
[strict-equal? -1 shift -1 -65]
[strict-equal? -1 shift -1 -64]
[strict-equal? -1 shift -1 -63]
[strict-equal? -1 shift -1 -62]
[strict-equal? -1 shift -1 -1]
[strict-equal? -1 shift -1 0]
[strict-equal? -2 shift -1 1]
#64bit
[strict-equal? to integer! #{c000000000000000} shift -1 62]
#64bit
[strict-equal? to integer! #{8000000000000000} shift -1 63]
[error? try [shift -1 64]]
[error? try [shift -1 65]]
#64bit
[error? try [shift -1 to integer! #{7ffffffffffffffe}]]
#64bit
[error? try [shift -1 to integer! #{7fffffffffffffff}]]
; arithmetic shift of 0
#64bit
[strict-equal? 0 shift 0 to integer! #{8000000000000000}]
#64bit
[strict-equal? 0 shift 0 to integer! #{8000000000000001}]
[strict-equal? 0 shift 0 -65]
[strict-equal? 0 shift 0 -64]
[strict-equal? 0 shift 0 -63]
[strict-equal? 0 shift 0 -62]
[strict-equal? 0 shift 0 -1]
[strict-equal? 0 shift 0 0]
[strict-equal? 0 shift 0 1]
[strict-equal? 0 shift 0 62]
[strict-equal? 0 shift 0 63]
[strict-equal? 0 shift 0 64]
[strict-equal? 0 shift 0 65]
#64bit
[strict-equal? 0 shift 0 to integer! #{7ffffffffffffffe}]
#64bit
[strict-equal? 0 shift 0 to integer! #{7fffffffffffffff}]
; arithmetic shift of 1
#64bit
[strict-equal? 0 shift 1 to integer! #{8000000000000000}]
#64bit
[strict-equal? 0 shift 1 to integer! #{8000000000000001}]
[strict-equal? 0 shift 1 -65]
[strict-equal? 0 shift 1 -64]
[strict-equal? 0 shift 1 -63]
[strict-equal? 0 shift 1 -62]
[strict-equal? 0 shift 1 -1]
[strict-equal? 1 shift 1 0]
[strict-equal? 2 shift 1 1]
#64bit
[strict-equal? to integer! #{4000000000000000} shift 1 62]
[error? try [shift 1 63]]
[error? try [shift 1 64]]
[error? try [shift 1 65]]
#64bit
[error? try [shift 1 to integer! #{7ffffffffffffffe}]]
#64bit
[error? try [shift 1 to integer! #{7fffffffffffffff}]]
; arithmetic shift of to integer! #{7ffffffffffffffe}
#64bit
[strict-equal? 0 shift to integer! #{7ffffffffffffffe} to integer! #{8000000000000000}]
#64bit
[strict-equal? 0 shift to integer! #{7ffffffffffffffe} to integer! #{8000000000000001}]
#64bit
[strict-equal? 0 shift to integer! #{7ffffffffffffffe} -65]
#64bit
[strict-equal? 0 shift to integer! #{7ffffffffffffffe} -64]
#64bit
[strict-equal? 0 shift to integer! #{7ffffffffffffffe} -63]
#64bit
[strict-equal? 1 shift to integer! #{7ffffffffffffffe} -62]
#64bit
[strict-equal? to integer! #{3fffffffffffffff} shift to integer! #{7ffffffffffffffe} -1]
#64bit
[strict-equal? to integer! #{7ffffffffffffffe} shift to integer! #{7ffffffffffffffe} 0]
#64bit
[error? try [shift to integer! #{7ffffffffffffffe} 1]]
#64bit
[error? try [shift to integer! #{7ffffffffffffffe} 62]]
#64bit
[error? try [shift to integer! #{7ffffffffffffffe} 63]]
#64bit
[error? try [shift to integer! #{7ffffffffffffffe} 64]]
#64bit
[error? try [shift to integer! #{7ffffffffffffffe} 65]]
#64bit
[error? try [shift to integer! #{7ffffffffffffffe} to integer! #{7ffffffffffffffe}]]
#64bit
[error? try [shift to integer! #{7ffffffffffffffe} to integer! #{7fffffffffffffff}]]
; arithmetic shift of to integer! #{7fffffffffffffff}
#64bit
[strict-equal? 0 shift to integer! #{7fffffffffffffff} to integer! #{8000000000000000}]
#64bit
[strict-equal? 0 shift to integer! #{7fffffffffffffff} to integer! #{8000000000000001}]
#64bit
[strict-equal? 0 shift to integer! #{7fffffffffffffff} -65]
#64bit
[strict-equal? 0 shift to integer! #{7fffffffffffffff} -64]
#64bit
[strict-equal? 0 shift to integer! #{7fffffffffffffff} -63]
#64bit
[strict-equal? 1 shift to integer! #{7fffffffffffffff} -62]
#64bit
[strict-equal? to integer! #{3fffffffffffffff} shift to integer! #{7fffffffffffffff} -1]
#64bit
[strict-equal? to integer! #{7fffffffffffffff} shift to integer! #{7fffffffffffffff} 0]
#64bit
[error? try [shift to integer! #{7fffffffffffffff} 1]]
#64bit
[error? try [shift to integer! #{7fffffffffffffff} 62]]
#64bit
[error? try [shift to integer! #{7fffffffffffffff} 63]]
#64bit
[error? try [shift to integer! #{7fffffffffffffff} 64]]
#64bit
[error? try [shift to integer! #{7fffffffffffffff} 65]]
#64bit
[error? try [shift to integer! #{7fffffffffffffff} to integer! #{7ffffffffffffffe}]]
#64bit
[error? try [shift to integer! #{7fffffffffffffff} to integer! #{7fffffffffffffff}]]

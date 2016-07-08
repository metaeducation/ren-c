; functions/context/set.r
; bug#1763
[a: 1 all [error? try [set [a] reduce [()]] a = 1]]
[a: 1 attempt [set [a b] reduce [2 ()]] a = 1]
[x: has [a: 1] all [error? try [set x reduce [()]] x/a = 1]]
[x: has [a: 1 b: 2] all [error? try [set x reduce [3 ()]] x/a = 1]]
; set [:get-word] [word]
[a: 1 b: _ set [:b] [a] b =? 1]
[unset 'a b: _ all [error? try [set [:b] [a]] blank? b]]
[unset 'a b: _ set/opt [:b] [a] void? get/opt 'b]

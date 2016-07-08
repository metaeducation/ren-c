; datatypes/native.r
[function? :reduce]
[not function? 1]
[function! = type-of :reduce]
; bug#1659
; natives are active
[same? blank! do reduce [:type-of make blank! blank]]

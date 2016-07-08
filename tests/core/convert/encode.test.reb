; functions/convert/encode.r
[binary? encode 'bmp make image! 10x20]
; bug#2040
[binary? encode 'png make image! 10x20]

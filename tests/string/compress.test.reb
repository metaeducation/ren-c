; functions/string/compress.r
; bug#1679
[#{666F6F} = decompress/gzip compress/gzip "foo"]

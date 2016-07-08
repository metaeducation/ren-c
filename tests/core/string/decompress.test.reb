; functions/string/decompress.r
; bug#1679: "Native GZIP compress/decompress suport"
["foo" == to string! decompress/gzip compress/gzip "foo"]
; bug#1679
["foo" == to string! decompress/gzip #{1F8B0800EF46BE4C00034BCBCF07002165738C03000000}]
; bug#3
[error? try [decompress #{AAAAAAAAAAAAAAAAAAAA}]]

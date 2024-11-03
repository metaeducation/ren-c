; functions/string/decompress.r
[#1679 ; "Native GZIP compress/decompress suport"
    ("foo" == decode 'UTF-8 gunzip gzip "foo")
]
[#1679 (
    data: #{1F8B0800EF46BE4C00034BCBCF07002165738C03000000}
    "foo" == decode 'UTF-8 gunzip data
)]
[#3
    ~bad-compression~ !! (inflate #{AAAAAAAAAAAAAAAAAAAA})
]

~bad-parameter~ !! (inflate:adler #{AAAAAAAAAAAAAAAAAAAA})

~???~ !! (gunzip #{AAAAAAAAAAAAAAAAAAAA})

; functions/string/decompress.r
[#1679 ; "Native GZIP compress/decompress suport"
    ("foo" == to text! gunzip gzip "foo")
]
[#1679
    ("foo" == to text! gunzip #{1F8B0800EF46BE4C00034BCBCF07002165738C03000000})
]
[#3
    ~bad-compression~ !! (inflate #{AAAAAAAAAAAAAAAAAAAA})
]

~bad-parameter~ !! (inflate:adler #{AAAAAAAAAAAAAAAAAAAA})

~???~ !! (gunzip #{AAAAAAAAAAAAAAAAAAAA})

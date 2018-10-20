; functions/string/compress.r
[#1679
    (#{666F6F} == inflate deflate "foo")
]

(#{666F6F} == zinflate zdeflate "foo")

(#{666F6F} == gunzip gzip "foo")

; Note: must use file that compresses to trigger DEFLATE usage, else the data
; will be STORE-d.  Assume %core-tests.r gets some net compression ratio.
(
    list: compose [
        %abc.txt #{616263} %test.r (read %core-tests.r) %def.txt #{646566}
    ]
    zip (zipped: copy #{}) list
    unzip (unzipped: copy []) zipped
    unzipped == list
)

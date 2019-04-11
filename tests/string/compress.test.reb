; functions/string/compress.r
[#1679
    (#{666F6F} = inflate deflate "foo")
]

(#{666F6F} = zinflate zdeflate "foo")

(#{666F6F} = gunzip gzip "foo")

; Note: must use file that compresses to trigger DEFLATE usage, else the data
; will be STORE-d.  Assume %core-tests.r gets some net compression ratio.
(
    str: "This is a test of a string that is long enough to use DEFLATE"
    list: compose [
        %abc.txt (str) %test.r (read %core-tests.r) %def.txt #{646566}
    ]
    zip (zipped: copy #{}) list
    unzip (unzipped: copy []) zipped
    did all [
        unzipped/1 = %abc.txt
        unzipped/2 = to binary! str
        (next next unzipped) = (next next list)
    ]
)

;  test a "foreign" file
(
    did all [
        unzip (unzipped: copy []) %fixtures/test.docx
    ]
)

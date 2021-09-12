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
        %abc.txt (str) %test.r (read %../core-tests.r) %def.txt #{646566}
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
        unzip (unzipped: copy []) %../fixtures/test.docx
    ]
)

; A 326-byte tricky file created problems for Red with zlib inflation, and
; either crashed it or gave inconsistent results.  It could be successfully
; inflated with Linux tool `zlib-flate -uncompress`, to 338 bytes.
;
; https://gitter.im/red/help?at=6139358f99b7d97528fc7152
;
; Ren-C got an error, which turns out to be Z_BUF_ERROR (-5).  Research
; suggests this is the correct response and that both Red and zlib-flate are
; incorrect in handling the corrupt file.  The tool `pigz` returns:
;
;     $ pigz -d < corrupt-zdeflated.bin
;     pigz: skipping: <stdin>: corrupted -- incomplete deflate data
;
; The stream seems to be missing a termination signal.  Reporting it to
; zlib-flate, it's believed to be a bug to accept the file:
;
; https://github.com/qpdf/qpdf/issues/562
;
; So now it's a check that there *is* an error.
(
    corrupt: read %../fixtures/corrupt-zdeflated.bin
    assert [326 = length of corrupt]

    e: trap [zinflate corrupt]
    e.id = 'bad-compression
)

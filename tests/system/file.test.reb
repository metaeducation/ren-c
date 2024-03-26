; system/file.r

(#{C3A4C3B6C3BC} == read %fixtures/umlauts-utf8.txt)
("äöü" == read/string %fixtures/umlauts-utf8.txt)
(["äöü"] == read/lines %fixtures/umlauts-utf8.txt)

; UTF-8 With Byte-Order Mark, not transparent in READ, #2280

(#{EFBBBFC3A4C3B6C3BC} == read %fixtures/umlauts-utf8bom.txt)
("^(FEFF)äöü" == read/string %fixtures/umlauts-utf8bom.txt)
(["^(FEFF)äöü"] == read/lines %fixtures/umlauts-utf8bom.txt)

; Byte order mark only transparent via LOAD with text codecs supporting it

(#{FFFEE400F600FC00} == read %fixtures/umlauts-utf16le.txt)
("äöü" == load/type %fixtures/umlauts-utf16le.txt 'utf-16le)

(#{FEFF00E400F600FC} == read %fixtures/umlauts-utf16be.txt)
("äöü" == load/type %fixtures/umlauts-utf16be.txt 'utf-16be)

; No codec support started yet for UTF-32

(#{FFFE0000E4000000F6000000FC000000} == read %fixtures/umlauts-utf32le.txt)
(#{0000FEFF000000E4000000F6000000FC} == read %fixtures/umlauts-utf32be.txt)

(block? read %./)
(block? read %fixtures/)

; These save tests were living in %mezz-save.r, but did not have expected
; outputs.  Moved here with expected binary result given by R3-Alpha.

[
    (data: [1 1.2 10:20 "test" user@example.com [sub block]]
    true)

    ((save blank []) = #{
    0A
    })

    ((save blank data) = #{
    3120312E322031303A3230202274657374222075736572406578616D706C652E
    636F6D205B73756220626C6F636B5D0A
    })

    ((save/header blank data [title: "my code"]) = #{
    5245424F4C205B0A202020207469746C653A20226D7920636F6465220A5D0A31
    20312E322031303A3230202274657374222075736572406578616D706C652E63
    6F6D205B73756220626C6F636B5D0A
    })
]

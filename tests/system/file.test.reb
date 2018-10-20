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

    ((save blank []) == #{
    0A
    })

    ((save blank data) == #{
    3120312E322031303A3230202274657374222075736572406578616D706C652E
    636F6D205B73756220626C6F636B5D0A
    })

    ((save/header blank data [title: "my code"]) == #{
    5245424F4C205B0A202020207469746C653A20226D7920636F6465220A5D0A31
    20312E322031303A3230202274657374222075736572406578616D706C652E63
    6F6D205B73756220626C6F636B5D0A
    })

    ; According to Mark Adler, of zlib fame:
    ;
    ; "Different compressors, or the same compressor at different settings,
    ; or even the same compressor with the same settings, but different
    ; versions, can produce different compressed output from the same input."
    ;
    ; So do not test for exact matches of BINARY! products of compression.
    ; Decompression should be consistent, however.

    ([] == load (save/compress blank [] true))

    ([] == load #{
    5245424F4C205B0A202020206F7074696F6E733A205B636F6D70726573735D0A
    5D0A1F8B080000000000000AE302009306D73201000000
    })

    (data == load (save/compress blank data true))

    (data == load #{
    5245424F4C205B0A202020206F7074696F6E733A205B636F6D70726573735D0A
    5D0A1F8B080000000000000A335430D433523034B0323250502A492D2E515228
    2D4E2D7248AD48CC2DC849D54BCECF55882E2E4D5248CAC94FCE8EE502000B38
    8CB030000000
    })

    (data == load (save/compress blank data 'script))

    (
        data == load #{
            5245424F4C205B0A202020206F7074696F6E733A205B636F6D70726573735D0A
            5D0A3634237B0A483473494141414141414141436A4E554D4E517A556A413073
            44497955464171535330755556496F4C553474636B6974534D777479456E5653
            383750565967750A4C6B3153534D724A5438364F35514941437A694D73444141
            4141413D0A7D
        }
    )

    (
        loaded: load/header (
            save/header/compress blank data [title: "my code"] true
        )
        header: take loaded
        did all [
            header/title == "my code"
            header/options == [compress]
            loaded == data
        ]
    )

    (
        loaded: load/header #{
            5245424F4C205B0A202020207469746C653A20226D7920636F6465220A202020
            206F7074696F6E733A205B636F6D70726573735D0A5D0A1F8B08000000000000
            0A335430D433523034B0323250502A492D2E5152282D4E2D7248AD48CC2DC849
            D54BCECF55882E2E4D5248CAC94FCE8EE502000B388CB030000000
        }
        header: take loaded
        did all [
            header/title == "my code"
            header/options == [compress]
            loaded == data
        ]
    )

    (
        loaded: load/header (
            save/header/compress blank data [title: "my code"] 'script
        )
        header: take loaded
        did all [
            header/title == "my code"
            header/options == [compress]
            loaded == data
        ]
    )

    (
        loaded: load/header #{
            5245424F4C205B0A202020207469746C653A20226D7920636F6465220A202020
            206F7074696F6E733A205B636F6D70726573735D0A5D0A3634237B0A48347349
            4141414141414141436A4E554D4E517A556A4130734449795546417153533075
            5556496F4C553474636B6974534D777479456E5653383750565967750A4C6B31
            53534D724A5438364F35514941437A694D734441414141413D0A7D
        }
        header: take loaded
        did all [
            header/title == "my code"
            header/options == [compress]
            loaded == data
        ]
    )

    (
        loaded: load/header (
            save/header blank data [title: "my code" options: [compress]]
        )
        header: take loaded
        did all [
            header/title == "my code"
            header/options == [compress]
            loaded == data
        ]
    )

    (
        loaded: load/header #{
            5245424F4C205B0A202020207469746C653A20226D7920636F6465220A202020
            206F7074696F6E733A205B636F6D70726573735D0A5D0A1F8B08000000000000
            0A335430D433523034B0323250502A492D2E5152282D4E2D7248AD48CC2DC849
            D54BCECF55882E2E4D5248CAC94FCE8EE502000B388CB030000000
        }
        header: take loaded
        did all [
            header/title == "my code"
            header/options == [compress]
            loaded == data
        ]
    )
]

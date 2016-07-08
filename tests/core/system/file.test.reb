; system/file.r
[#{C3A4C3B6C3BC} == read %fixtures/umlauts-utf8.txt]
["äöü" == read/string %fixtures/umlauts-utf8.txt]
[["äöü"] == read/lines %fixtures/umlauts-utf8.txt]
[#{EFBBBFC3A4C3B6C3BC} == read %fixtures/umlauts-utf8bom.txt]
["äöü" == read/string %fixtures/umlauts-utf8bom.txt]
[["äöü"] == read/lines %fixtures/umlauts-utf8bom.txt]
[#{FFFEE400F600FC00} == read %fixtures/umlauts-utf16le.txt]
["äöü" == read/string %fixtures/umlauts-utf16le.txt]
[["äöü"] == read/lines %fixtures/umlauts-utf16le.txt]
[#{FEFF00E400F600FC} == read %fixtures/umlauts-utf16be.txt]
["äöü" == read/string %fixtures/umlauts-utf16be.txt]
[["äöü"] == read/lines %fixtures/umlauts-utf16be.txt]
[#{FFFE0000E4000000F6000000FC000000} == read %fixtures/umlauts-utf32le.txt]
[#{0000FEFF000000E4000000F6000000FC} == read %fixtures/umlauts-utf32be.txt]
[block? read %./]
[block? read %fixtures/]

; We put the tests that take a long time and stress at the end.  They may need
; their own separate file.


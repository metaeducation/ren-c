; (Technically ADLER32 is in core, but test it in Crypt)

; NOTE: most hashes you find on the web show the result in big-endian.
; However, they are usually presented as integers in hex, e.g. 0x01020304
; In todays world, those are usually binary-encoded as little endian, so
; when the interface is a BINARY! then this may make more sense.  Review.

; ADLER32 tests in Go (could borrow from)
; https://golang.org/src/hash/adler32/adler32_test.go

(#{62006200} = checksum-core 'adler32 "a")
(#{5D06FB3A} = checksum-core 'adler32 "Mark Adler is cool")

; Make sure it works in the Crypt extension wrapper as well

(#{62006200} = checksum 'adler32 "a")
(#{5D06FB3A} = checksum 'adler32 "Mark Adler is cool")

; Test :PART, which should skip codepoint counts in strings and byte
; counts in BINARY!.  Cat emoji is 4 codepoints.
[
    (text: {high🐱codepoint}
    bin: as binary! text
    bin = #{68696768F09F90B1636F6465706F696E74})

    (#{6C04BC16} = checksum-core:part 'adler32 (skip text 4) 5)
    (#{6C04BC16} = checksum-core:part 'adler32 (skip bin 4) 8)
]

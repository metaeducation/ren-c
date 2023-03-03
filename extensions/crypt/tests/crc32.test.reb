; (CRC32 is in CHECKSUM-CORE too, but test it in Crypt)

(#{4F57A50D} = checksum-core 'crc32 "More tests needed")
(#{4F57A50D} = checksum 'crc32 "More tests needed")

(#{2165738C} = checksum 'CRC32 to-binary "foo")
(#{00000000} = checksum 'CRC32 to-binary "")

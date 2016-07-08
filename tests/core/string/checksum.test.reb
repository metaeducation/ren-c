; functions/string/checksum.r
[#{ACBD18DB4CC2F85CEDEF654FCCC4A4D8} = checksum/method to-binary "foo" 'md5]
[#{FC3FF98E8C6A0D3087D515C0473F8677} = checksum/method to-binary "hello world!" 'md5]
[#{0BEEC7B5EA3F0FDBC95D0DD47F3C5BC275DA8A33} = checksum/method to-binary "foo" 'sha1]
[#{430CE34D020724ED75A196DFC2AD67C77772D169} = checksum/method to-binary "hello world!" 'sha1]
; bug#1678: "Can we add CRC-32 as a checksum method?"
[(checksum/method to-binary "foo" 'CRC32) = -1938594527]
; bug#1678
[(checksum/method to-binary "" 'CRC32) = 0]

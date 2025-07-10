; Need more SHA-1 tests...

(#{0BEEC7B5EA3F0FDBC95D0DD47F3C5BC275DA8A33}
    = checksum 'sha1 encode 'UTF-8 "foo")

(#{430CE34D020724ED75A196DFC2AD67C77772D169}
    = checksum 'sha1 encode 'UTF-8 "hello world!")

;; Tests for text-lines.reb
;; Included as they are part of the build and source tests.

[; Setup test.
    do %../make/tools/common.r
    do repo/tools/text-lines.reb
    true
]

;; encode-lines

[quote {**^/} = encode-lines copy {} {**} {  }]
[quote {**  x^/} = encode-lines copy {x} {**} {  }]
[quote {**  x^/**^/} = encode-lines copy {x^/} {**} {  }]
[quote {**^/**  x^/} = encode-lines copy {^/x} {**} {  }]
[quote {**^/**  x^/**^/} = encode-lines copy {^/x^/} {**} {  }]
[quote {**  x^/**    y^/**      z^/} = encode-lines copy {x^/  y^/    z} {**} {  }]
[quote "**^/**^/**^/" = encode-lines copy {^/^/} {**} {  }]

;; decode-lines

[quote {} = decode-lines copy {} {**} {} ]
[quote {} = decode-lines copy {**^/} {**} {  } ]
[quote {x} = decode-lines copy {**  x^/} {**} {  } ]
[quote {x^/} = decode-lines copy {**  x^/**^/} {**} {  } ]
[quote {^/x} = decode-lines copy {**^/**  x^/} {**} {  } ]
[quote {^/x^/} = decode-lines copy {**^/**  x^/**^/} {**} {  } ]
[quote {x^/  y^/    z} = decode-lines copy {**  x^/**    y^/**      z^/} {**} {  } ]
[quote {^/^/} = decode-lines copy "**^/**  ^/**^/" {**} {  }]
[quote {^/^/} = decode-lines copy "**^/**^/**^/" {**} {  }]

;; lines-exceeding

[blank? lines-exceeding 0 {}]
[blank? lines-exceeding 1 {}]
[[1] = lines-exceeding 0 {x}]
[[2] = lines-exceeding 0 {^/x}]

;; text-line-of

[blank? text-line-of {}]
[1 = text-line-of {x}]
[1 = text-line-of next {x^/}]
[2 = text-line-of next next {x^/y}]
[2 = text-line-of next next {x^/y^/z}]
[2 = text-line-of next next next {x^/y^/}]
[2 = text-line-of next next next {x^/y^/z}]

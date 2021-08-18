; Tests for text-lines.reb
; Included as they are part of the build and source tests.

(; Setup test.
    import %../../tools/common.r
    import %../../tools/text-lines.reb
    true
)

; encode-lines

({**^/} = encode-lines copy {} {**} {  })
({**  x^/} = encode-lines copy {x} {**} {  })
({**  x^/**^/} = encode-lines copy {x^/} {**} {  })
({**^/**  x^/} = encode-lines copy {^/x} {**} {  })
({**^/**  x^/**^/} = encode-lines copy {^/x^/} {**} {  })
({**  x^/**    y^/**      z^/} = encode-lines copy {x^/  y^/    z} {**} {  })
("**^/**^/**^/" = encode-lines copy {^/^/} {**} {  })

; decode-lines

({} = decode-lines copy {} {**} {} )
({} = decode-lines copy {**^/} {**} {  } )
({x} = decode-lines copy {**  x^/} {**} {  } )
({x^/} = decode-lines copy {**  x^/**^/} {**} {  } )
({^/x} = decode-lines copy {**^/**  x^/} {**} {  } )
({^/x^/} = decode-lines copy {**^/**  x^/**^/} {**} {  } )
({x^/  y^/    z} = decode-lines copy {**  x^/**    y^/**      z^/} {**} {  } )
({^/^/} = decode-lines copy "**^/**  ^/**^/" {**} {  })
({^/^/} = decode-lines copy "**^/**^/**^/" {**} {  })

; lines-exceeding

(null? lines-exceeding 0 {})
(null? lines-exceeding 1 {})
([1] = lines-exceeding 0 {x})
([2] = lines-exceeding 0 {^/x})

; text-line-of

(null? text-line-of {})
(1 = text-line-of {x})
(1 = text-line-of next {x^/})
(2 = text-line-of next next {x^/y})
(2 = text-line-of next next {x^/y^/z})
(2 = text-line-of next next next {x^/y^/})
(2 = text-line-of next next next {x^/y^/z})

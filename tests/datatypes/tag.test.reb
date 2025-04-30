(tag? <tag>)
(not tag? 1)
(tag! = type of <tag>)

(tag? to tag! "")
(strict-equal? to tag! "" make tag! 0)
(strict-equal? to tag! "" to tag! "")
("<tag>" = mold <tag>)

[#219
    ("<ēee>" = mold <ēee>)
]

; Just make sure recursive molding doesn't hang...
(
    block-1: reduce ['a _]
    block-2: reduce ['b block-1]
    block-1.2: block-2
    all [
        block-1.1 = block-1.1
        block-1.2 = block-1.2
    ]
)

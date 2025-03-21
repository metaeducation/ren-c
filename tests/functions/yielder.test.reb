; YIELDER is the basis of GENERATOR, which allows a parameterized call to
; each invocation of the routine.  GENERATOR simply has an empty spec for
; making it convenient to call (like DOES)



; Parallel to:
; https://www.geeksforgeeks.org/coroutine-in-python/
;
; We use NULL as the signal to close the coroutines when they are passing
; parameters between them, vs. going with close()... the NULL tells them
; to close themselves.
(
    stuff: []
    /log: func [x] [
        if block? x [x: compose x]
        append stuff x
    ]

    /producer: func [sentence [text!] next-coroutine [action!]] [
        log <start-producing>
        let tokens: split sentence space
        for-each 'token tokens [
            log [produce: (token)]
            next-coroutine token  ; Python says `next_coroutine.send(token)`
        ]
        next-coroutine null  ; Python says `next_coroutine.close()`
        log <end-producing>
    ]

    /pattern-filter: func [next-coroutine [action!] :pattern [text!]] [
        pattern: default ["ing"]

        return yielder [token [~null~ text!]] [
            log <start-filtering>
            while [token] [  ; Python does a blocking `token = (yield)`
                log [filter: (token)]

                if find token pattern [
                    next-coroutine token
                ]
                yield ~  ; yield to producer, so it can call w/another token
            ]
            log <done-filtering>
            next-coroutine null
            yield:final ~
        ]
    ]

    /emit-token: yielder [token [~null~ text!]] [
        log <start-emitting>
        while [token] [  ; Python does a blocking `token = (yield)`
            log [emit: (token)]

            log token
            yield ~  ; yield to pattern-filter, so it can call w/another token
        ]
        log <done-emitting>
        yield:final ~
    ]

    /et: emit-token/
    /pf: pattern-filter et/

    sentence: "Bob is running behind a fast moving car"
    producer sentence pf/

    stuff = [
        <start-producing>
        [produce: "Bob"]
        <start-filtering>
        [filter: "Bob"]
        [produce: "is"]
        [filter: "is"]
        [produce: "running"]
        [filter: "running"]
        <start-emitting>
        [emit: "running"]
        "running"
        [produce: "behind"]
        [filter: "behind"]
        [produce: "a"]
        [filter: "a"]
        [produce: "fast"]
        [filter: "fast"]
        [produce: "moving"]
        [filter: "moving"]
        [emit: "moving"]
        "moving"
        [produce: "car"]
        [filter: "car"]
        <done-filtering>
        <done-emitting>
        <end-producing>
    ]
)

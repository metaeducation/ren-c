Rebol [
    title: "Line number"
    file: %line-numberq.r
    copyright: [2012 "Saphirion AG"]
    license: {
        Licensed under the Apache License, Version 2.0 (the "License");
        you may not use this file except in compliance with the License.
        You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
    }
    author: "Ladislav Mecir"
    purpose: "Compute the line number"
]

line-number?: func [
    s [text! blob!]
    <local> t line-number
] [
    line-number: 1
    t: head of s
    return parse2 t [
        opt some [
            (if greater-or-equal? index? t index? s [return line-number])
            [[CR LF | CR | LF] (line-number: me + 1) | one] t:
        ]
    ]
]

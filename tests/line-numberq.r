Rebol [
    Title: "Line number"
    File: %line-numberq.r
    Copyright: [2012 "Saphirion AG"]
    License: {
        Licensed under the Apache License, Version 2.0 (the "License");
        you may not use this file except in compliance with the License.
        You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "Ladislav Mecir"
    Purpose: "Compute the line number"
]

line-number?: func [
    s [text! binary!]
    <local> t line-number
] [
    line-number: 1
    t: head of s
    parse2 t [
        opt some [
            (if greater-or-equal? index? t index? s [return line-number])
            [[CR LF | CR | LF] (line-number: me + 1) | one] t:
        ]
    ]
]

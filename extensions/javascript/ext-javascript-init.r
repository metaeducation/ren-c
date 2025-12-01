Rebol [
    title: "JavaScript Natives Usermode Support Code"

    name: Javascript
    type: module

    version: 0.1.0
    date: 15-Sep-2018

    rights: "Copyright (C) 2018-2019 hostilefork.com"

    license: "LGPL 3.0"
]

export js-awaiter: specialize js-native/ [awaiter: okay]

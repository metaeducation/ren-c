REBOL [
    Title: "JavaScript Natives Usermode Support Code"

    Name: Javascript
    Type: Module

    Version: 0.1.0
    Date: 15-Sep-2018

    Rights: "Copyright (C) 2018-2019 hostilefork.com"

    License: {LGPL 3.0}
]

init-javascript-extension


js-awaiter: :js-native/awaiter

sys/export [js-native js-awaiter]  ; !!! Hacky export scheme

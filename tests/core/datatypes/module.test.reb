; datatypes/module.r
[module? module [] []]
[not module? 1]
[module! = type-of module [] []]
[
    a-module: module [
    ] [
        ; 'var will be in the module
        var: 1
    ]
    var: 2
    1 == a-module/var
]
; import test
[
    a-module: module [
        exports: [var]
    ] [
        var: 2
    ]
    import a-module
    2 == var
]
; import test
[
    var: 1
    a-module: module [
        exports: [var]
    ] [
        var: 2
    ]
    import a-module
    1 == var
]

tests: [
"MOLD-STACK TESTS:^/"
"nested ajoin..."
[ nested-ajoin: func [n] [
    either n <= 1 [n]
    [ajoin [n space nested-ajoin n - 1]]
  ]
  "9 8 7 6 5 4 3 2 1" = nested-ajoin 9
]
"mold recursive object..."
[ o: object [a: 1 r: none] o/r: o
  "<make object! [^/    a: 1^/    r: make object! [...]^/]>" = ajoin ["<" mold o  ">"]]
"form recursive object..."
[ o: object [a: 1 r: none] o/r: o
  "<a: 1^/r: make object! [...]>" = ajoin ["<" form o  ">"]]
"detab..."
[ "<aa  b   c>" = ajoin ["<" detab "aa^-b^-c" ">"]]
"entab..."
[ "<^- a    b>" = ajoin ["<" entab "     a    b" ">"]]
"dehex..."
[ "<a b>" = ajoin ["<" dehex "a%20b" ">"]]
"form..."
[ {<1 <a> 2 3 ">} = ajoin ["<" form [1 <a> [2 3] "^""] ">"]]
"transcode..."
[ "<[a [b c] #{}]>" = ajoin ["<" mold transcode to binary! "a [b c]"  ">"]]
"..."
[ "<>" = probe ajoin ["<" intersect [a b c] [b c d]  ">"]]
] ; end tests

; MAIN
count: failed: 0
for-each t tests [
  unless block? t [prin t continue]
  either (time: dt [t: do t] t)
  [print time]
  [print 'FAILED! ++ failed]
  ++ count
]
print [
  either failed > 0 ['FAILED]['failed]
  #":"
  failed #"/" count
]

; vim: set syn=rebol expandtab sw=2 nosmartindent autoindent:

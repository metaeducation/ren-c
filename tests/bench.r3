REBOL [
Title: "Benchmark program"
Author: "Ladislav Mecir"
Date: 5-Oct-2010/11:36:11+2:00
File: %bench.r
Purpose: "Runs several benchmarks"
]
tick-time: 0.01
time-block: func [
"Time a block."
block [block!]
precision [decimal!] "suggested value: 0.05 to 0.30"
/verbose
/local guess count start finish time result
] [
if verbose [print ["Timing a block:" mold block]]
guess: 0
count: 1
while [
start: now/precise
repeat :count :block
finish: now/precise
time: to decimal! difference finish start
result: time / count
if verbose [
prin "Iterations: "
prin count
prin ". Time/iteration: "
prin result
prin " seconds.^/"
]
any [
result <= 0 (abs result - guess) / result + (tick-time / time * 4) > precision
]
] [
guess: result
if error? try [count: count * 2] [return]
]
result
]
sieve: func [size /local flags i prime series] [
flags: make block! :size
change/dup :flags :true :size
while [not tail? :flags] [
if first :flags [
i: index? :flags
prime: :i + :i + 1
series: skip :flags (:prime * :i)
while [not tail? :series] [
change :series :false
series: skip :series :prime
]
]
flags: next :flags
]
head :flags
]
fourbang: func [
/local
ten
one
temp
] [
ten: 10.0
one: 1.0
temp: ten
temp: temp + one
temp: temp - one
temp: temp * ten
temp: temp / ten
temp: temp - one
temp: temp * ten
temp: temp + ten
temp: temp / ten
temp: temp + one
temp: temp - one
temp: temp * ten
temp: temp / ten
temp: temp - one
temp: temp * ten
temp: temp + ten
temp: temp / ten
temp: temp + one
temp: temp - one
temp: temp * ten
temp: temp / ten
temp: temp - one
temp: temp * ten
temp: temp + ten
temp: temp / ten
temp: temp + one
temp: temp - one
temp: temp * ten
temp: temp / ten
temp: temp - one
temp: temp * ten
temp: temp + ten
temp: temp / ten
]
gqf2: func [
"Gaussian quadrature formula of the second order"
func [any-function!] "function to compute a definite integral of"
a [number!] "starting point of the integration interval"
b [number!] "end point of the integration interval"
n [integer!] "number of subintervals"
/local h m sum alpha beta sqrt3 halfh
] [
h: (b - a) / n
halfh: h / 2
m: 0
sum: 0
sqrt3: 1 / (square-root 3)
alpha: a + (halfh * (1 - sqrt3))
beta: a + (halfh * (1 + sqrt3))
while [:n > :m] [
sum: :sum + (func :alpha) + (func :beta)
alpha: :alpha + :h
beta: :beta + :h
m: :m + 1
]
sum: :halfh * :sum
]
msort: func [
"Merge-sort a series in place."
a [series!]
compare [any-function!]
/local msort-do merge
] [
msort-do: func [a l /local mid b] [
either l <= 2 [
unless any [
l < 2
compare first a second a
] [
set/any 'b first a
change/only a second a
change/only next a get/any 'b
]
] [
mid: to integer! l / 2
msort-do a mid
msort-do skip a mid l - mid
merge a mid skip a mid l - mid
]
]
merge: func [
{Uses auxiliary storage, at most half the size of the sorted series.}
a la b lb /local c
] [
c: copy/part a la
loop-until [
either (compare first b first c) [
change/only a first b
b: next b
a: next a
zero? lb: lb - 1
] [
change/only a first c
c: next c
a: next a
empty? c
]
]
change a c
]
msort-do a length? a
a
]
set-words: func [
"Get all set-words from a block"
block [block!]
/deep "also search in subblocks/parens"
/local elem words rule here
] [
words: make block! length? block
rule: either deep [[
any [
set elem set-word! (
insert tail words to word! :elem
) | here: [block! | paren!] :here into rule | skip
]
]] [[
any [
set elem set-word! (
insert tail words to word! :elem
) | skip
]
]]
parse3 block rule
words
]
cfor: func [
"General loop" [throw]
init [block!]
test [block!]
inc [block!]
body [block!]
] [
use set-words init reduce [
:do init
:while test head insert tail copy body inc
]
]
enum: function [
"Enumerates a block"
from [integer!]
to [integer!]
] [result] [
result: make block! to + 1 - from
cfor [i: from] [i <= to] [i: i + 1] [
insert tail result i
]
result
]
locals?: func [
"Get all locals from a spec block."
spec [block!]
/args "get only arguments"
/local locals item item-rule
] [
locals: make block! 16
item-rule: either args [[
refinement! to end (item-rule: [end skip]) |
set item any-word! (insert tail locals to word! :item) | skip
]] [[
set item any-word! (insert tail locals to word! :item) | skip
]]
parse3 spec [any item-rule]
locals
]
funcs: func [
{Define a function with auto local and static variables.} [throw]
spec [block!] {Help string (opt) followed by arg words with opt type and string}
init [block!] "Set-words become static variables, shallow scan"
body [block!] "Set-words become local variables, deep scan"
/local svars lvars
] [
spec: copy spec
init: copy/deep init
body: copy/deep body
svars: set-words init
lvars: set-words/deep body
unless empty? svars [
use svars reduce [reduce [init body]]
]
unless empty? lvars: exclude exclude lvars locals? spec svars [
insert any [find spec /local insert tail spec /local] lvars
]
do init
make function! reduce [spec body]
]
round-place: funcs [
x [number!]
place [integer!]
/ceiling "round up"
/floor "round down"
] [] [
scale: 10.0 ** place
x: either place <= 0 [
if (abs x) + scale - (abs x) = 0 [return x]
scale: 10.0 ** negate place
x * scale
] [
x / scale
]
r: x // 1.0
s: case [
floor [either r >= 0 [0.0] [-1.0]]
ceiling [either r > 0 [1.0] [0.0]]
r >= 0.0 [
case [
r > 0.5 [1.0]
r < 0.5 [0.0]
x // 2.0 = 0.5 [0.0]
true [1.0]
]
]
r < -0.5 [-1.0]
r > -0.5 [0.0]
x // 2.0 = -0.5 [0.0]
true [-1.0]
]
either place <= 0 [x + s - r / scale] [x + s - r * scale]
]
autoround: funcs [[catch]
x [number!] "number to round"
digits [integer!] "digits to keep"
/ceiling "round up"
/floor "round down"
] [] [
if digits < 1 [throw make error! "digits needs to be >= 1"]
if zero? x [return x]
place: round/floor/to log-10 abs x 1
if positive? 10.0 ** place - abs x [place: place - 1]
place: place - digits + 1
case [
floor [round-place/floor x place]
ceiling [round-place/ceiling x place]
true [round-place x place]
]
]
random/seed 1
use [computer precision os size flags t count result sinerad icount serf compare mcount] [
prin "Benchmark run "
prin now
prin ". Rebol "
print Rebol/version
prin "Computer: "
computer: input
prin "OS: "
os: input
precision: make decimal! ask "Precision: "
prin "Empty block: "
t: time-block [] precision
print unspaced [autoround 1 / t 3 "Hz"]
size: 8190
prin unspaced ["Eratosthenes Sieve Prime (size: " size "): "]
t: time-block [flags: sieve size] precision
count: 0
foreach flag flags [
if flag [count: count + 1]
]
print unspaced [
autoround 1 / t 3
"Hz, result: "
count
" primes"
]
prin "Four-Banger test (+,-,*,/): "
t: time-block [result: fourbang] precision
print unspaced [
autoround 1 / t 3
"Hz, result: "
result
]
icount: 10000
prin unspaced ["Integral (icount: " icount ") of sin(x) 0<=x<=pi/2: "]
sinerad: func [x] [sine (x * 180 / pi)]
t: time-block [result: gqf2 :sinerad 0 (pi / 2) icount] precision
print unspaced [
autoround 1 / t 3
"Hz, result: "
result
]
prin unspaced ["Integral (icount: " icount ") of exp(x) 0<=x<=1: "]
t: time-block [result: gqf2 :exp 0 1 icount] precision
print unspaced [
autoround 1 / t 3
"Hz, result: "
result
]
mcount: 500
prin unspaced [
"Merge Sort ("
mcount
" elements): "
]
compare: func [a b] [
return a <= b
]
b: random enum 1 mcount
t: time-block [msort copy b :compare] precision
print unspaced [
autoround 1 / t 3
"Hz"
]
]

; datatypes/integer.r
(integer? 0)
[#33
    (integer? -0)
]
(not integer? 1.1)
(integer! = type of 0)
(integer? 1)
(integer? -1)
(integer? 2)
; 32bit minimum
(integer? -2147483648)
; 32bit maximum
(integer? 2147483647)
; 64bit minimum
<64bit>
(integer? -9223372036854775808)
; 64bit maximum
<64bit>
(integer? 9223372036854775807)

~bad-make-arg~ !! (0 = make integer! 0)
(0 = make integer! "0")
(0 = to integer! 0)
(0 = to integer! "0")

(-2147483648 = make integer! -2147483648.0)
(-2147483648 = make integer! -2147483648.9)
(2147483647 = make integer! 2147483647.9)
<32bit>
(error? trap [make integer! -2147483649.0])
<32bit>
(error? trap [make integer! 2147483648.0])
[#921
    ~overflow~ !! (make integer! 9.2233720368547765e18)
    ~overflow~ !! (make integer! -9.2233720368547779e18)
]

~expect-arg~ !! (to integer! null)
~expect-arg~ !! (to integer! okay)

(0 = codepoint of NUL)
(1 = codepoint of #"^a")
(0 = to integer! #0)
(1 = to integer! #1)
(302961000000 = make integer! "3.02961E+11")
(error? trap [to integer! "t"])
("0" = mold 0)
("1" = mold 1)
("-1" = mold -1)

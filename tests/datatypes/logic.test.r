; datatypes/logic.r
(logic? okay)
(logic? null)
(not logic? 1)
(keyword! = type of okay)
(null = try type of null)

((on? 'on) = true? 'true)
((on? 'off) = true? 'false)
((yes? 'yes) = true? 'true)
((yes? 'no) = true? 'false)

(okay = to-logic 0)
(okay = to-logic 1)
(okay = to-logic "f")

(null = null-if-zero 0)
(okay = null-if-zero 1)

~expect-arg~ !! (mold okay)
~expect-arg~ !! (mold null)

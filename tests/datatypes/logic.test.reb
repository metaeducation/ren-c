; datatypes/logic.r

(logic? okay)
(logic? null)
(not logic? 1)

(okay = okay)
(null = null)

(okay = to-logic 0)
(okay = to-logic 1)
(okay = to-logic "f")

(null = logical 0)
(okay = logical 1)
(okay = logical -1)

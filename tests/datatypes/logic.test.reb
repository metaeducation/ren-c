; datatypes/logic.r

(logic? okay)
(logic? null)
(not logic? 1)

(okay = okay)
(null = null)

(null = to-logic null)
(okay = to-logic okay)
(okay = to-logic 0)
(okay = to-logic 1)
(okay = to-logic "f")

(okay = logical okay)
(null = logical null)
(okay = logical 0)
(okay = logical 1)
(okay = logical "f")

(null = int-to-logic 0)
(okay = int-to-logic 1)
(okay = int-to-logic -1)

(didn't null)
(didn't void)
(not didn't 1020)

(not did null)
(not did ())
(did 1020)

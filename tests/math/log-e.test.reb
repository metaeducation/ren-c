; functions/math/log-e.r
(0 = log-e 1)
(0.5 = log-e square-root 2.718281828459045)
(1 = log-e 2.718281828459045)
(-1 = log-e 1 / 2.718281828459045)
(2 = log-e 2.718281828459045 * 2.718281828459045)
(error? try [log-e 0])
(error? try [log-e -1])

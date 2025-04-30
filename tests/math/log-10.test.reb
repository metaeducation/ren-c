; functions/math/log-10.r
(0 ?= log-10 1)
(0.5 ?= log-10 square-root 10)
(1 ?= log-10 10)
(-1 ?= log-10 0.1)
(2 ?= log-10 100)
(-2 ?= log-10 0.01)
(3 ?= log-10 1000)
(-3 ?= log-10 0.001)

~positive~ !! (log-10 0)
~positive~ !! (log-10 -1)

; functions/math/log-e.r
(0 is log-e 1)
(0.5 is log-e square-root 2.718281828459045)
(1 is log-e 2.718281828459045)
(-1 is log-e 1 / 2.718281828459045)
(2 is log-e 2.718281828459045 * 2.718281828459045)
(error? trap [log-e 0])
(error? trap [log-e -1])

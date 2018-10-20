; functions/math/multiply.r
<32bit>
(error? trap [multiply -2147483648 -2147483648])
<32bit>
(error? trap [multiply -2147483648 -1073741824])
<32bit>
(error? trap [multiply -2147483648 -2])
<32bit>
(error? trap [multiply -2147483648 -1])
(0 == multiply -2147483648 0)
(-2147483648 == multiply -2147483648 1)
<32bit>
(error? trap [multiply -2147483648 2])
<32bit>
(error? trap [multiply -2147483648 1073741824])
<32bit>
(error? trap [multiply -2147483648 2147483647])
<32bit>
(error? trap [multiply -1073741824 -2147483648])
<32bit>
(error? trap [multiply -1073741824 -1073741824])
<32bit>
(error? trap [multiply -1073741824 -2])
(1073741824 == multiply -1073741824 -1)
(0 == multiply -1073741824 0)
(-1073741824 == multiply -1073741824 1)
(-2147483648 == multiply -1073741824 2)
<32bit>
(error? trap [multiply -1073741824 1073741824])
<32bit>
(error? trap [multiply -1073741824 2147483647])
<32bit>
(error? trap [multiply -2 -2147483648])
<32bit>
(error? trap [multiply -2 -1073741824])
(4 == multiply -2 -2)
(2 == multiply -2 -1)
(0 == multiply -2 0)
(-2 == multiply -2 1)
(-4 == multiply -2 2)
(-2147483648 == multiply -2 1073741824)
<32bit>
(error? trap [multiply -2 2147483647])
<32bit>
(error? trap [multiply -1 -2147483648])
(1073741824 == multiply -1 -1073741824)
(2 == multiply -1 -2)
(1 == multiply -1 -1)
(0 == multiply -1 0)
(-1 == multiply -1 1)
(-2 == multiply -1 2)
(-1073741824 == multiply -1 1073741824)
(-2147483647 == multiply -1 2147483647)
(0 == multiply 0 -2147483648)
(0 == multiply 0 -1073741824)
(0 == multiply 0 -2)
(0 == multiply 0 -1)
(0 == multiply 0 0)
(0 == multiply 0 1)
(0 == multiply 0 2)
(0 == multiply 0 1073741824)
(0 == multiply 0 2147483647)
(-2147483648 == multiply 1 -2147483648)
(-1073741824 == multiply 1 -1073741824)
(-2 == multiply 1 -2)
(-1 == multiply 1 -1)
(0 == multiply 1 0)
(1 == multiply 1 1)
(2 == multiply 1 2)
(1073741824 == multiply 1 1073741824)
(2147483647 == multiply 1 2147483647)
<32bit>
(error? trap [multiply 2 -2147483648])
(-2147483648 == multiply 2 -1073741824)
(-4 == multiply 2 -2)
(-2 == multiply 2 -1)
(0 == multiply 2 0)
(2 == multiply 2 1)
<32bit>
(error? trap [multiply 2 1073741824])
<32bit>
(error? trap [multiply 2 2147483647])
<32bit>
(error? trap [multiply 1073741824 -2147483648])
<32bit>
(error? trap [multiply 1073741824 -1073741824])
(-2147483648 == multiply 1073741824 -2)
(-1073741824 == multiply 1073741824 -1)
(0 == multiply 1073741824 0)
(1073741824 == multiply 1073741824 1)
<32bit>
(error? trap [multiply 1073741824 2])
<32bit>
(error? trap [multiply 1073741824 1073741824])
<32bit>
(error? trap [multiply 1073741824 2147483647])
<32bit>
(error? trap [multiply 2147483647 -2147483648])
<32bit>
(error? trap [multiply 2147483647 -1073741824])
<32bit>
(error? trap [multiply 2147483647 -2])
(-2147483647 == multiply 2147483647 -1)
(0 == multiply 2147483647 0)
(2147483647 == multiply 2147483647 1)
<32bit>
(error? trap [multiply 2147483647 2])
<32bit>
(error? trap [multiply 2147483647 1073741824])
<32bit>
(error? trap [multiply 2147483647 2147483647])
<64bit>
(error? trap [multiply -1 -9223372036854775808])
<64bit>
(error? trap [multiply -9223372036854775808 -1])
(0:0:1 == multiply 0:0:2 0.5)

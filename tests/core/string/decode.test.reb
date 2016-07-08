; functions/string/decode.r
[image? decode 'bmp read %fixtures/rebol-logo.bmp]
[image? decode 'gif read %fixtures/rebol-logo.gif]
[image? decode 'jpeg read %fixtures/rebol-logo.jpg]
[image? decode 'png read %fixtures/rebol-logo.png]
["" == decode 'text #{}]
["bar" == decode 'text #{626172}]

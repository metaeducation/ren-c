; datatypes/port.r
[port? make port! http://]
[not port? 1]
[port! = type-of make port! http://]

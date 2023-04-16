; datatypes/port.r
(port? make port! http://)
(not port? 1)
(port! = kind of make port! http://)

; %dns.test.reb
;
; (This is a minimal test, but better than zero tests, which was the case in
; the original R3-Alpha suite.)


; Note that not all Domains are required to have reverse DNS lookup offered.
; (e.g. dns://example.com will not do a reverse lookup on the tuple you get)
; https://networkengineering.stackexchange.com/q/25421/
;
(did all [
    tuple? address: read dns://rebol.com
    "rebol.com" = read join dns:// address
])

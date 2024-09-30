; %dns.test.reb
;
; (This is a minimal test, but better than zero tests, which was the case in
; the original R3-Alpha suite.)


; Note that not all Domains are required to have reverse DNS lookup offered.
; (e.g. dns://example.com will not do a reverse lookup on the tuple you get)
; https://networkengineering.stackexchange.com/q/25421/
;
(all wrap [
    tuple? address: read dns://rebol.com
    "rebol.com" = read join dns:// address
])

; Reading dns:// has been standardized in Ren-C to try and get the real
; IP address of the system.  On Windows this is as easy as passing null to
; gethostbyname().  On Linux it fabricates a connection to Google's 8.8.8.8
; DNS and asks what its side of that connection is.
;
; (Historically Rebol2 on Windows gave back the Windows machine name, R3-Alpha
; on Linux gave an error, etc.)
;
(tuple? read dns://)

## DNS Extension

R3-Alpha's DNS "port" was written to do forward and reverse resolves on
either URL! literals (a subclass of ANY-STRING?) or the TUPLE! datatype (a
fairly narrow type, that could be a few byte-sized integers...used also for
color constants).

    >> read dns://rebol.com
    == 162.216.18.225

    >> read dns://162.216.18.225
    == "rebol.com"

The registration of handlers in Rebol for "schemes" (the "dns" preceding the
colon in `dns://`) connected the native port code to manage these requests.
Due to the "WinSock" API, the approaches for doing these calls can be nearly
equivalent on Windows and POSIX.

Ren-C moves this code into an extension so that no part of it will be built
into a core that does not need it.

## Synchronous vs. Asynchronous

While the basic APIs for dealing with DNS lookup are synchronous, Windows
offered WSAAsyncGetHostByName() as an alternative.  This allowed a single
threaded application to make an asynchronous DNS request...posting a requested
WM_USER message ID to the message pump when the request was fulfilled.

Despite this being a Windows-only feature, R3-Alpha implemented it under a
conditional define (HAS_ASYNC_DNS).  However, Microsoft deprecated this API,
offering no IPv6 equivalent.  They instead suggested that applications that
want asynchronous lookup are expected to use threads and call the standard
getnameinfo() API.

Considering the relatively low priority of the feature and its use of a
deprecated API, Ren-C removed the code--focusing instead on trying to clarify
the port model and its synchronous/asynchronous modes in a more forward
looking way.

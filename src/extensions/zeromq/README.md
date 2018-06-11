This is a 2018 adaptation of Andreas Bolka's zmqext/r3-zmq extension:

https://github.com/earl/r3-zmq

The original code was written to the COMMAND! interface of R3-Alpha.  This
version attempts to use the "libRebol" API of Ren-C, which has significantly
more rigor in its model of interaction with the garbage collector, and also
permits the free mixture of inline Rebol code with spliced API handles.

Below are Andreas's original notes:

======

A REBOL 3 extension providing a low-level binding to the ØMQ library.


Status
------

This extension is currently developed and tested against ØMQ 2.1.3. The full
API of 2.1.3 is implemented. The basic functionality is tested as working, but
the code-base is still young and no automated and extensive test-suite is in
place.

r3-zmq should already be usable for most basic scenarios. REP/REQ is
illustrated by `hwserver.r3`/`hwclient.r3` and `wuserver.r3`/`wuclient.r3` form
a basic PUB/SUB pair. `taskvent.r3`/`taskwork.r3`/`tasksink.r3` demonstrate a
two-stage PUSH/PULL chain.


License
-------

Copyright (C) 2011 Andreas Bolka, Licensed under the terms of the Apache
License, Version 2.0 (see LICENSE.zmqext).

This extension uses the ØMQ library (libzmq), which is licensed under the terms
of the GNU Lesser General Public License (LGPL), Version 3 (see
LICENSE.libzmq). For more details on the ØMQ library license, consult
http://www.zeromq.org/area:licensing.

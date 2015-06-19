REBOL [
	Title: "Saphir host init code generator"
	Author: "Richard Smolak"
	Copyright: {2010 2011 2012 2013 Saphirion AG, Zug, Switzerland}
	License: {
		Licensed under the Apache License, Version 2.0.
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
]

do %make-host-init.r

; Files to include in the host program:
files: [
	%mezz/prot-tls.r
	%mezz/prot-http.r
	%mezz/saphir-patches.r	
]

code: load-files files

save %boot/host-init.r code

write-c-file %include/host-init.h code

set REBOL=r3-view.exe
set T=../../../src/tools
set OS_ID=0.3.3
set HOST_PRODUCT_NAME=atronix-view

%REBOL% %T%/make-headers.r
%REBOL% %T%/make-boot.r %OS_ID% %HOST_PRODUCT_NAME%
%REBOL% %T%/make-os-ext.r
%REBOL% %T%/make-reb-lib.r
%REBOL% %T%/core-ext.r
%REBOL% %T%/view-ext.r
%REBOL% %T%/saphir-init.r

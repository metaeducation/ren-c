#include "reb-host.h"

#define INCLUDE_EXT_DATA
#include "host-ext-licensing.h"

#include "../../c-code/extensions/licensing/src/odprintf.c"
#include "../../c-code/extensions/licensing/src/licensing.c"
#include "../../c-code/extensions/licensing/src/r3-ext.c"


extern int RX_Call(int cmd, RXIFRM *frm, void *data);

RL_LIB *RL; // Link back to reb-lib from embedded extensions

/***********************************************************************
**
*/	void Init_Licensing_Ext(void)
/*
**	Initialize special variables of the core extension.
**
***********************************************************************/
{
	RL = RL_Extend((REBYTE *)(&RX_licensing[0]), (RXICAL)&RX_Call);
}

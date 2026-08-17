#include "i_net.h"

#include <string.h>

#include "d_net.h"
#include "doomstat.h"

extern doomcom_t *doomcom;

static doomcom_t singleplayer_doomcom;

void I_InitNetwork(void)
{
    // No network support for ESP32 v1.
    memset(&singleplayer_doomcom, 0, sizeof(singleplayer_doomcom));
    singleplayer_doomcom.id = DOOMCOM_ID;
    singleplayer_doomcom.numnodes = 1;
    singleplayer_doomcom.ticdup = 1;
    singleplayer_doomcom.numplayers = 1;
    singleplayer_doomcom.consoleplayer = 0;
    doomcom = &singleplayer_doomcom;
    netgame = false;
}

void I_NetCmd(void)
{
}

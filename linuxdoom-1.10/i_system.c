// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: m_bbox.c,v 1.1 1997/02/03 22:45:10 b1 Exp $";


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>

#include "doomdef.h"

#ifdef DOOM_ESP32
#include "platform/platform_time.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#include "m_misc.h"
#include "i_video.h"
#include "i_sound.h"

#include "d_net.h"
#include "g_game.h"

#ifdef __GNUG__
#pragma implementation "i_system.h"
#endif
#include "i_system.h"




int	mb_used = 6;


void
I_Tactile
( int	on,
  int	off,
  int	total )
{
  // UNUSED.
  (void)on;
  (void)off;
  (void)total;
}

ticcmd_t	emptycmd;
ticcmd_t*	I_BaseTiccmd(void)
{
    return &emptycmd;
}


int  I_GetHeapSize (void)
{
    return mb_used*1024*1024;
}

byte* I_ZoneBase (int*	size)
{
    byte* zone;

    *size = mb_used*1024*1024;
#ifdef DOOM_ESP32
    zone = (byte *)heap_caps_malloc(*size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    zone = (byte *) malloc (*size);
#endif

    if (!zone)
    {
	I_Error ("Unable to allocate the %i-byte DOOM zone", *size);
	return NULL;
    }

    return zone;
}



//
// I_GetTime
// returns time in 1/70th second tics
//
int  I_GetTime (void)
{
#ifdef DOOM_ESP32
    uint32_t newms;
    static uint32_t basems = 0;

    newms = platform_millis();
    if (!basems)
        basems = newms;
    return (int)((uint64_t)(newms - basems) * TICRATE / 1000U);
#else
    struct timeval	tp;
    struct timezone	tzp;
    int			newtics;
    static int		basetime=0;
  
    gettimeofday(&tp, &tzp);
    if (!basetime)
	basetime = tp.tv_sec;
    newtics = (tp.tv_sec-basetime)*TICRATE + tp.tv_usec*TICRATE/1000000;
    return newtics;
#endif
}



//
// I_Init
//
void I_Init (void)
{
    I_InitSound();
    //  I_InitGraphics();
}

//
// I_Quit
//
void I_Quit (void)
{
    D_QuitNetGame ();
    I_ShutdownSound();
    I_ShutdownMusic();
    M_SaveDefaults ();
    I_ShutdownGraphics();
    exit(0);
}

void I_WaitVBL(int count)
{
#ifdef DOOM_ESP32
    uint32_t delay_ms = (uint32_t)((count * 1000U) / 70U);

    vTaskDelay(pdMS_TO_TICKS(delay_ms ? delay_ms : 1));
#else
#ifdef SGI
    sginap(1);                                           
#else
#ifdef SUN
    sleep(0);
#else
    usleep (count * (1000000/70) );                                
#endif
#endif
#endif
}

void I_BeginRead(void)
{
}

void I_EndRead(void)
{
}

byte*	I_AllocLow(int length)
{
    byte*	mem;

#ifdef DOOM_ESP32
    mem = (byte *)heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    mem = (byte *)malloc (length);
#endif
    if (!mem)
    {
	I_Error ("Unable to allocate %i bytes", length);
	return NULL;
    }
    memset (mem,0,length);
    return mem;
}


//
// I_Error
//
extern boolean demorecording;

void I_Error (char *error, ...)
{
    va_list	argptr;

    // Message first.
    va_start (argptr,error);
    fprintf (stderr, "Error: ");
    vfprintf (stderr,error,argptr);
    fprintf (stderr, "\n");
    va_end (argptr);

    fflush( stderr );

    // Shutdown. Here might be other errors.
    if (demorecording)
	G_CheckDemoStatus();

    D_QuitNetGame ();
    I_ShutdownGraphics();

#ifdef DOOM_ESP32
    fprintf (stderr, "DOOM stopped after a fatal error; reset the board to retry.\n");
    fflush (stderr);
    for (;;)
        vTaskDelay(portMAX_DELAY);
#else
    exit(-1);
#endif
}

/*  Copyright 2003-2005 Guillaume Duhamel
    Copyright 2004-2006 Theo Berkau

    This file is part of Yabause.

    Yabause is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Yabause is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yabause; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/
/*
        Copyright 2019 devMiyax(smiyaxdev@gmail.com)

This file is part of YabaSanshiro.

        YabaSanshiro is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

YabaSanshiro is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
along with YabaSanshiro; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#ifndef VDP1_H
#define VDP1_H

#include "memory.h"

#define VIDCORE_DEFAULT         -1  
#define VIDCORE_DUMMY           0

//#define YAB_ASYNC_RENDERING 1

#if defined (__cplusplus)
extern "C"{
#endif

typedef struct {
   u16 TVMR;
   u16 FBCR;
   u16 PTMR;
   u16 EWDR;
   u16 EWLR;
   u16 EWRR;
   u16 ENDR;
   u16 EDSR;
   u16 LOPR;
   u16 COPR;
   u16 MODR;

   u32 addr;
   int disptoggle_dont_use_me; // not used anymore, see Vdp1External_struct

   s16 localX;
   s16 localY;

   u16 systemclipX1;
   u16 systemclipY1;
   u16 systemclipX2;
   u16 systemclipY2;

   u16 userclipX1;
   u16 userclipY1;
   u16 userclipX2;
   u16 userclipY2;
} Vdp1;

typedef struct
{
   int id;
   const char *Name;
   int (*Init)(void);
   void (*DeInit)(void);
   void (*Resize)(int,int,unsigned int, unsigned int, int, int);
   int (*IsFullscreen)(void);
   // VDP1 specific
   int (*Vdp1Reset)(void);
   void (*Vdp1DrawStart)(void);
   void (*Vdp1DrawEnd)(void);
   void(*Vdp1NormalSpriteDraw)(u8 * ram, Vdp1 * regs, u8 * back_framebuffer);
   void(*Vdp1ScaledSpriteDraw)(u8 * ram, Vdp1 * regs, u8 * back_framebuffer);
   void(*Vdp1DistortedSpriteDraw)(u8 * ram, Vdp1 * regs, u8 * back_framebuffer);
   void(*Vdp1PolygonDraw)(u8 * ram, Vdp1 * regs, u8 * back_framebuffer);
   void(*Vdp1PolylineDraw)(u8 * ram, Vdp1 * regs, u8 * back_framebuffer);
   void(*Vdp1LineDraw)(u8 * ram, Vdp1 * regs, u8 * back_framebuffer);
   void(*Vdp1UserClipping)(u8 * ram, Vdp1 * regs);
   void(*Vdp1SystemClipping)(u8 * ram, Vdp1 * regs);
   void(*Vdp1LocalCoordinate)(u8 * ram, Vdp1 * regs);
   void(*Vdp1ReadFrameBuffer)(u32 type, u32 addr, void * out);
   void(*Vdp1WriteFrameBuffer)(u32 type, u32 addr, u32 val);
   void(*Vdp1EraseWrite)(void);
   void(*Vdp1FrameChange)(void);
   // VDP2 specific
   int (*Vdp2Reset)(void);
   void (*Vdp2DrawStart)(void);
   void (*Vdp2DrawEnd)(void);
   void (*Vdp2DrawScreens)(void);
   void (*GetGlSize)(int *width, int *height);
   void (*SetSettingValue)(int type, int value);
   void(*Sync)();
   void (*GetNativeResolution)(int *width, int *height, int * interlace);
   void(*Vdp2DispOff)(void);
   void(*OnUpdateColorRamWord)(u32 addr);
   void (*GetScreenshot)(void ** outbuf, int * width, int * height);
} VideoInterface_struct;

extern VideoInterface_struct *VIDCore;
extern VideoInterface_struct VIDDummy;

extern u8 * Vdp1Ram;
/* Pointer from which the render thread reads the command list (snapshot of Vdp1Ram). */
extern u8 * Vdp1RamRender;
/* Geometry-relevant registers taken from the snapshot. If snapshotting is off,
   Vdp1RenderRegsValid stays 0 and the render path reads the live registers. */
extern int Vdp1RenderRegsValid;
extern u16 Vdp1RenderTVMR;
extern u16 Vdp1RenderFBCR;
void Vdp1RamSnapAllNew(void);

// Texture cache persistence: write stamps for VDP1 VRAM. v1_page_seq holds one
// monotonic stamp per 4 KiB page (512 KiB / 4 KiB = 128), set on every VRAM write.
// The reader is the tile cache (yglcache.c): entries stay valid as long as their
// source pages have not been rewritten since creation (gen) and gen >= v1_flush_seq
// (set on reset and savestate load). Deliberately without atomics (writer: emulation
// thread, reader: render thread); a late-seen stamp can only invalidate one frame
// too late, never validate too early.
extern u32 v1_page_seq[128];
extern u32 v1_wseq;
extern u32 v1_flush_seq;
void Vdp1MarkVramWrite(u32 addr, u32 size);
void Vdp1TexStampFlush(void);

u8 FASTCALL	Vdp1RamReadByte(u32);
u16 FASTCALL	Vdp1RamReadWord(u32);
u32 FASTCALL	Vdp1RamReadLong(u32);
void FASTCALL	Vdp1RamWriteByte(u32, u8);
void FASTCALL	Vdp1RamWriteWord(u32, u16);
void FASTCALL	Vdp1RamWriteLong(u32, u32);
u8 FASTCALL Vdp1FrameBufferReadByte(u32);
u16 FASTCALL Vdp1FrameBufferReadWord(u32);
u32 FASTCALL Vdp1FrameBufferReadLong(u32);
void FASTCALL Vdp1FrameBufferWriteByte(u32, u8);
void FASTCALL Vdp1FrameBufferWriteWord(u32, u16);
void FASTCALL Vdp1FrameBufferWriteLong(u32, u32);

void Vdp1DrawCommands(u8 * ram, Vdp1 * regs, u8* back_framebuffer);
void Vdp1FakeDrawCommands(u8 * ram, Vdp1 * regs);

extern Vdp1 * Vdp1Regs;

enum VDP1STATUS {
  VDP1_STATUS_IDLE = 0,
  VDP1_STATUS_RUNNING
};

// struct for Vdp1 part that shouldn't be saved
typedef struct {
   int disptoggle;
   int manualerase;
   int manualchange;
   int vbalnk_erase;
   int frame_change_plot;
   int swap_frame_buffer;
   int current_frame;
   int status;
} Vdp1External_struct;

extern Vdp1External_struct Vdp1External;

typedef struct
{
   u16 CMDCTRL;
   u16 CMDLINK;
   u16 CMDPMOD;
   u16 CMDCOLR;
   u16 CMDSRCA;
   u16 CMDSIZE;
   s16 CMDXA;
   s16 CMDYA;
   s16 CMDXB;
   s16 CMDYB;
   s16 CMDXC;
   s16 CMDYC;
   s16 CMDXD;
   s16 CMDYD;
   u16 CMDGRDA;   
} vdp1cmd_struct;

int Vdp1Init(void);
void Vdp1DeInit(void);
int VideoInit(int coreid);
int VideoChangeCore(int coreid);
void VideoDeInit(void);
void Vdp1Reset(void);


u8 FASTCALL	Vdp1ReadByte(u32);
u16 FASTCALL	Vdp1ReadWord(u32);
u32 FASTCALL	Vdp1ReadLong(u32);
void FASTCALL	Vdp1WriteByte(u32, u8);
void FASTCALL	Vdp1WriteWord(u32, u16);
void FASTCALL	Vdp1WriteLong(u32, u32);

void Vdp1Draw(void);
void Vdp1NoDraw(void);
void FASTCALL Vdp1ReadCommand(vdp1cmd_struct *cmd, u32 addr, u8* ram);

int Vdp1SaveState(FILE *fp);
int Vdp1LoadState(FILE *fp, int version, int size);

char *Vdp1DebugGetCommandNumberName(u32 number);
void Vdp1DebugCommand(u32 number, char *outstring);
u32 *Vdp1DebugTexture(u32 number, int *w, int *h);
void ToggleVDP1(void);

#if defined (__cplusplus)
}
#endif

#endif

/*  Copyright 2003-2005 Guillaume Duhamel
    Copyright 2004 Lawrence Sebald
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

/*! \file vdp1.c
    \brief VDP1 emulation functions.
*/


#include <time.h>
#include <stdlib.h>
#include "yabause.h"
#include "vdp1.h"
#include "debug.h"
#include "scu.h"
#include "vdp2.h"
#include "vidsoft.h"
#include "threads.h"
#include "sh2core.h"
#include <atomic>
#include <condition_variable>
#include <chrono>
extern "C" { extern volatile int g_ptmr_this_frame; }   /* vdp2.cpp */


using std::atomic;
using std::condition_variable;
using std::mutex;
namespace chrono = std::chrono;

extern "C" {
u8 * Vdp1Ram;

/* Snapshot of the VDP1 command list for the render thread. The render thread
reads commands from the live Vdp1Ram while the SH2 on the emulation thread may
already be writing the next frame's commands. A command is 32 bytes and the four
vertices sit at bytes 12..27, so a writer in the middle produces a quad with
three old and one new vertex (stretched wedges in Fighters Megamix and Fighting
Vipers); an unfinished command can drop a body part entirely. The software
renderer already uses such a snapshot (vidsoft.c). One buffer is enough because
Vdp1JoinPendingRenders() (vdp2.cpp) runs before every dispatch, so no render is
active while the snapshot is taken. With snapshotting off, Vdp1RamRender points
at Vdp1Ram itself. */
u8 * Vdp1RamRender = NULL;
static u8 * Vdp1RamSnap = NULL;

/* TVMR and FBCR determine the frame geometry (width, height, interlace; see
vidogl.c) and are written by the SH2 and read by the renderer, so they are
snapshotted together with the RAM. If a game changed them mid-draw the geometry
would jump within a frame. */
int Vdp1RenderRegsValid = 0;
u16 Vdp1RenderTVMR = 0;
u16 Vdp1RenderFBCR = 0;

/* Copy only what changed. Copying the full 512 KB every frame would flush the
256 KB L2 twice. Every VDP1 write therefore marks its 4 KB page (128 pages), and
the snapshot copies only the marked ones. Deliberately not hooked into
Vdp1MarkVramWrite(), whose bookkeeping depends on g_texcache_persist. */
#define V1SNAP_PAGES 128            /* 0x80000 / 4096 */
static u8  v1_snap_dirty[V1SNAP_PAGES];
static int v1_snap_all = 1;        /* first snapshot: copy everything */

/* Mark everything as changed after reset and savestate load, since Vdp1Ram is
overwritten there without going through the write functions. */
extern "C" void Vdp1RamSnapAllNew(void) { v1_snap_all = 1; }

/* Shared by both drawing paths: the VBlank-OUT path (vdp2.cpp) and the immediate
draw path via PTMR=1 / VDPEV_DIRECT_DRAW (further down in this file). */
extern "C" void Vdp1RamSnapshot(void) {
   if (Vdp1RamSnap == NULL || Vdp1Ram == NULL) return;
   if (v1_snap_all) {
      memcpy(Vdp1RamSnap, Vdp1Ram, 0x80000);
      v1_snap_all = 0;
      memset(v1_snap_dirty, 0, sizeof(v1_snap_dirty));
   } else {
/* copy contiguous runs in one go instead of 128 separate memcpy calls */
      u32 i = 0;
      while (i < V1SNAP_PAGES) {
         if (!v1_snap_dirty[i]) { i++; continue; }
         u32 a = i;
         while (i < V1SNAP_PAGES && v1_snap_dirty[i]) { v1_snap_dirty[i] = 0; i++; }
         memcpy(Vdp1RamSnap + (a << 12), Vdp1Ram + (a << 12), (size_t)(i - a) << 12);
      }
   }
/* also take the two geometry-relevant registers */
   Vdp1RenderTVMR = Vdp1Regs->TVMR;
   Vdp1RenderFBCR = Vdp1Regs->FBCR;
   Vdp1RenderRegsValid = 1;
   Vdp1RamRender = Vdp1RamSnap;
}
u8 * Vdp1FrameBuffer[2];
VideoInterface_struct *VIDCore = NULL;
extern VideoInterface_struct *VIDCoreList[];
extern YabEventQueue * rcv_evqueue;
Vdp1 * Vdp1Regs;
Vdp1External_struct Vdp1External = { 0 };
}

atomic<int> vdp1_clock{ 0 };
condition_variable vdp1_clock_cv;
mutex vdp1_clock_mtx;

// Texture cache persistence; see the comment in vdp1.h.
u32 v1_page_seq[128];
u32 v1_wseq;
u32 v1_flush_seq;

extern "C" int g_texcache_persist; // defined in yglcache.c (C symbol)


void Vdp1MarkVramWrite(u32 addr, u32 size) {
// If persistence is disabled, skip the stamp bookkeeping entirely (it runs on every
// VDP1 VRAM write).
   if (!g_texcache_persist) return;
/* The emulation thread stamps, the render thread reads; release stores make the
   stamps visible to it on ARM64. */
   u32 seq = ++v1_wseq;
   u32 end = (addr + size - 1) & 0x7FFFF;
   u32 pg0 = addr >> 12, pg1 = end >> 12;
/* v1_page_seq is read by YglRangeClean (yglcache.c). */
   __atomic_store_n(&v1_page_seq[pg0], seq, __ATOMIC_RELEASE);  
   if (pg1 != pg0) __atomic_store_n(&v1_page_seq[pg1], seq, __ATOMIC_RELEASE);
}

void Vdp1TexStampFlush(void) {
   v1_flush_seq = ++v1_wseq;
}


void Vdp1_onHblank() {
#if 0
  {
    std::unique_lock<std::mutex> lk(vdp1_clock_mtx);
    vdp1_clock += 100;
  }
  if (Vdp1External.status == VDP1_STATUS_RUNNING) {
#if defined(YAB_ASYNC_RENDERING)
    vdp1_clock_cv.notify_one();
#else
    Vdp1DrawCommands(Vdp1Ram, Vdp1Regs, NULL);
#endif
  }
#else
  vdp1_clock += 100;
#endif
}


//////////////////////////////////////////////////////////////////////////////

extern "C" u8 FASTCALL Vdp1RamReadByte(u32 addr) {
   addr &= 0x7FFFF;
   return T1ReadByte(Vdp1Ram, addr);
}

//////////////////////////////////////////////////////////////////////////////

extern "C" u16 FASTCALL Vdp1RamReadWord(u32 addr) {
    addr &= 0x07FFFF;
    return T1ReadWord(Vdp1Ram, addr);
}

//////////////////////////////////////////////////////////////////////////////

extern "C" u32 FASTCALL Vdp1RamReadLong(u32 addr) {
   addr &= 0x7FFFF;
   return T1ReadLong(Vdp1Ram, addr);
}

//////////////////////////////////////////////////////////////////////////////

extern "C" void FASTCALL Vdp1RamWriteByte(u32 addr, u8 val) {
   addr &= 0x7FFFF;
   T1WriteByte(Vdp1Ram, addr, val);
   vdp1_clock = 0;
   v1_snap_dirty[addr >> 12] = 1;
   Vdp1MarkVramWrite(addr, 1);
}

//////////////////////////////////////////////////////////////////////////////
extern "C" void FASTCALL Vdp1RamWriteWord(u32 addr, u16 val) {
   addr &= 0x7FFFF;
   T1WriteWord(Vdp1Ram, addr, val);
   vdp1_clock = 0;
   v1_snap_dirty[addr >> 12] = 1; v1_snap_dirty[((addr + 1) & 0x7FFFF) >> 12] = 1;
   Vdp1MarkVramWrite(addr, 2);
}

//////////////////////////////////////////////////////////////////////////////
extern "C" void FASTCALL Vdp1RamWriteLong(u32 addr, u32 val) {
   addr &= 0x7FFFF;
   //if(addr == 0x00000)
   //LOG("Vdp1RamWriteLong @ %08X", CurrentSH2->regs.PC);
   T1WriteLong(Vdp1Ram, addr, val);
   vdp1_clock = 0;
   v1_snap_dirty[addr >> 12] = 1; v1_snap_dirty[((addr + 3) & 0x7FFFF) >> 12] = 1;
   Vdp1MarkVramWrite(addr, 4);
}

//////////////////////////////////////////////////////////////////////////////

extern "C" u8 FASTCALL Vdp1FrameBufferReadByte(u32 addr) {
   addr &= 0x3FFFF;
   //if (VIDCore->Vdp1ReadFrameBuffer && addr < 0x30000 ){
   //  u8 val;
   //  VdpLockVram();
   //  VIDCore->Vdp1ReadFrameBuffer(0, addr, &val);
   //  VdpUnLockVram();
   //  return val;
   //}
   return T1ReadByte(Vdp1FrameBuffer[Vdp1External.current_frame], addr);
}

//////////////////////////////////////////////////////////////////////////////

extern "C" u16 FASTCALL Vdp1FrameBufferReadWord(u32 addr) {
   addr &= 0x3FFFF;
   if (VIDCore->Vdp1ReadFrameBuffer ){
     u16 val;
     VdpLockVram();
     VIDCore->Vdp1ReadFrameBuffer(1, addr, &val);
     VdpUnLockVram();
     return val;
   } 
   return T1ReadWord(Vdp1FrameBuffer[Vdp1External.current_frame], addr);
}

//////////////////////////////////////////////////////////////////////////////

extern "C" u32 FASTCALL Vdp1FrameBufferReadLong(u32 addr) {
   addr &= 0x3FFFF;
   if (VIDCore->Vdp1ReadFrameBuffer ){
     u32 val;
     VdpLockVram();
     VIDCore->Vdp1ReadFrameBuffer(2, addr, &val);
     VdpUnLockVram();
     return val;
   }
   return T1ReadLong(Vdp1FrameBuffer[Vdp1External.current_frame], addr);
}

//////////////////////////////////////////////////////////////////////////////

extern "C" void FASTCALL Vdp1FrameBufferWriteByte(u32 addr, u8 val) {
   addr &= 0x3FFFF;

   if (VIDCore->Vdp1WriteFrameBuffer)
   {
      VdpLockVram();
      VIDCore->Vdp1WriteFrameBuffer(0, addr, val);
      VdpUnLockVram();
   }

   T1WriteByte(Vdp1FrameBuffer[Vdp1External.current_frame], addr, val);
}

//////////////////////////////////////////////////////////////////////////////

extern "C" void FASTCALL Vdp1FrameBufferWriteWord(u32 addr, u16 val) {
   addr &= 0x3FFFF;

   if (VIDCore->Vdp1WriteFrameBuffer)
   {
      VdpLockVram();
      VIDCore->Vdp1WriteFrameBuffer(1, addr, val);
      VdpUnLockVram();
   }

   T1WriteWord(Vdp1FrameBuffer[Vdp1External.current_frame], addr, val);
}

//////////////////////////////////////////////////////////////////////////////

extern "C" void FASTCALL Vdp1FrameBufferWriteLong(u32 addr, u32 val) {
   addr &= 0x3FFFF;

   if (VIDCore->Vdp1WriteFrameBuffer)
   {
      VdpLockVram();
      VIDCore->Vdp1WriteFrameBuffer(2, addr, val);
      VdpUnLockVram();
   }

   T1WriteLong(Vdp1FrameBuffer[Vdp1External.current_frame], addr, val);
}

//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////

extern "C" int Vdp1Init(void) {
   if ((Vdp1Regs = (Vdp1 *) malloc(sizeof(Vdp1))) == NULL)
      return -1;

   if ((Vdp1Ram = T1MemoryInit(0x80000)) == NULL)
      return -1;

   /* The render pointer initially points at the real RAM; a snapshot buffer is allocated too. */
   Vdp1RamRender = Vdp1Ram;
   if (Vdp1RamSnap == NULL) Vdp1RamSnap = T1MemoryInit(0x80000);

   // Allocate enough memory for two frames
   if ((Vdp1FrameBuffer[0] = T1MemoryInit(0x40000)) == NULL)
      return -1;

   if ((Vdp1FrameBuffer[1] = T1MemoryInit(0x40000)) == NULL)
     return -1;

   Vdp1External.status = VDP1_STATUS_IDLE;
   Vdp1External.disptoggle = 1;

   Vdp1Regs->TVMR = 0;
   Vdp1Regs->FBCR = 0;
   Vdp1Regs->PTMR = 0;

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

extern "C" void Vdp1DeInit(void) {
   if (Vdp1Regs)
      free(Vdp1Regs);
   Vdp1Regs = NULL;

   if (Vdp1Ram)
      T1MemoryDeInit(Vdp1Ram);
   Vdp1Ram = NULL;
   if (Vdp1RamSnap)
      T1MemoryDeInit(Vdp1RamSnap);
   Vdp1RamSnap = NULL;
   Vdp1RamRender = NULL;

   if (Vdp1FrameBuffer[0])
      T1MemoryDeInit(Vdp1FrameBuffer[0]);
   if (Vdp1FrameBuffer[1])
     T1MemoryDeInit(Vdp1FrameBuffer[1]);

   Vdp1FrameBuffer[0] = NULL;
   Vdp1FrameBuffer[1] = NULL;
}

//////////////////////////////////////////////////////////////////////////////

extern "C" int VideoInit(int coreid) {
   return VideoChangeCore(coreid);
}

//////////////////////////////////////////////////////////////////////////////

extern "C" int VideoChangeCore(int coreid)
{
   int i;

   // Make sure the old core is freed
   VideoDeInit();

   // So which core do we want?
   if (coreid == VIDCORE_DEFAULT)
      coreid = 0; // Assume we want the first one

   // Go through core list and find the id
   for (i = 0; VIDCoreList[i] != NULL; i++)
   {
      if (VIDCoreList[i]->id == coreid)
      {
         // Set to current core
         VIDCore = VIDCoreList[i];
         break;
      }
   }

   if (VIDCore == NULL)
      return -1;

   if (VIDCore->Init() != 0)
      return -1;

   // Reset resolution/priority variables
   if (Vdp2Regs)
   {
      VIDCore->Vdp1Reset();
   }

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

extern "C" void VideoDeInit(void) {
   if (VIDCore)
      VIDCore->DeInit();
   VIDCore = NULL;
}

//////////////////////////////////////////////////////////////////////////////

/* Number of VDP1 draw commands per second. Always counted (the spr_* counters exist
   only in diagnostic builds). Evaluated and reset once per second in vdp2.cpp; the
   data race is harmless since it is only a heuristic. */

unsigned int g_v1_drawn = 0;



extern "C" void Vdp1Reset(void) {
  /* Flush the VDP1 texture-cache stamps so that no cache entry is considered
     clean after a reset or savestate load. */
  Vdp1TexStampFlush();
  Vdp1RamSnapAllNew();
  /* Vdp1Regs is a pointer, so sizeof(Vdp1Regs) would clear only a few fields; clear
     the whole structure (the buffer comes from malloc). */
  /* EWDR/EWLR/EWRR survive a reset (e.g. CKCHG) (Ymir); the first call still
     clears them (malloc leftovers). */
  static int vdp1_reset_count = 0;
  const u32 keep_ewdr = Vdp1Regs->EWDR, keep_ewlr = Vdp1Regs->EWLR, keep_ewrr = Vdp1Regs->EWRR;
  const int keep_ew = (vdp1_reset_count++ > 0);
  memset(Vdp1Regs, 0, sizeof(Vdp1));
   Vdp1Regs->PTMR = 0;
   Vdp1Regs->MODR = 0x1000; // VDP1 Version 1
   Vdp1Regs->TVMR = 0;
   Vdp1Regs->EWDR = 0;
   Vdp1Regs->EWLR = 0;
   Vdp1Regs->EWRR = 0;
   Vdp1Regs->ENDR = 0;
   if (keep_ew) {
      Vdp1Regs->EWDR = keep_ewdr; Vdp1Regs->EWLR = keep_ewlr; Vdp1Regs->EWRR = keep_ewrr;
   }
   VIDCore->Vdp1Reset();

   Vdp1Regs->userclipX1 = 0;
   Vdp1Regs->userclipY1 = 0;
   Vdp1Regs->userclipX2 = 0;
   Vdp1Regs->userclipY2 = 0;
   Vdp1Regs->systemclipX1 = 0;
   Vdp1Regs->systemclipY1 = 0;
   Vdp1Regs->systemclipX2 = 0;
   Vdp1Regs->systemclipY2 = 0;

   // Safe tarminator for Radient silvergun with no bios
   T1WriteWord(Vdp1Ram, 0x40000, 0x8000);

   vdp1_clock = 0;

}

extern "C" int VideoSetSetting( int type, int value )
{
	if (VIDCore) VIDCore->SetSettingValue( type, value );
	return 0;
}


//////////////////////////////////////////////////////////////////////////////

extern "C" u8 FASTCALL Vdp1ReadByte(u32 addr) {
   addr &= 0xFF;
   /* byte from the 16-bit register (Ymir) */
   return (u8)(Vdp1ReadWord(addr & ~1u) >> ((~addr & 1) * 8));
}

//////////////////////////////////////////////////////////////////////////////

extern "C" u16 FASTCALL Vdp1ReadWord(u32 addr) {
   addr &= 0xFF;
   switch(addr) {
      case 0x10:
        FRAMELOG("Read EDSR %X line = %d\n", Vdp1Regs->EDSR, yabsys.LineCount);
        return Vdp1Regs->EDSR;
      case 0x12:
        FRAMELOG("Read LOPR %X line = %d\n", Vdp1Regs->LOPR, yabsys.LineCount);
        return Vdp1Regs->LOPR;
      case 0x14:
        FRAMELOG("Read COPR %X line = %d\n", Vdp1Regs->COPR, yabsys.LineCount);
        return Vdp1Regs->COPR;
      case 0x16: {
        u16 mode = 0x1000 | ((Vdp1Regs->PTMR & 2) << 7) | ((Vdp1Regs->FBCR & 0x1E) << 3) | (Vdp1Regs->TVMR & 0xF);
        FRAMELOG("Read MODR %X line = %d\n", mode, yabsys.LineCount);
        return mode;
      }
      default:
         LOG("trying to read a Vdp1 write-only register\n");
   }
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

extern "C" u32 FASTCALL Vdp1ReadLong(u32 addr) {
   addr &= 0xFF;
   LOG("trying to long-read a Vdp1 register - %08X\n", addr);
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

extern "C" void FASTCALL Vdp1WriteByte(u32 addr, u8 val) {
   addr &= 0xFF;
   /* read-modify-write of the 16-bit register (Ymir) */
   const u16 shift = (u16)((~addr & 1) * 8);
   u16 word = Vdp1ReadWord(addr & ~1u);
   word = (u16)((word & ~(0xFF << shift)) | ((u16)val << shift));
   Vdp1WriteWord(addr & ~1u, word);
}

extern YabEventQueue * vdp1_rcv_evqueue;
extern "C" void Vdp1MarkRenderDispatched(void);

//////////////////////////////////////////////////////////////////////////////

extern "C" void FASTCALL Vdp1WriteWord(u32 addr, u16 val) {
  addr &= 0xFF;
  switch(addr) {
    case 0x0:
      Vdp1Regs->TVMR = val;
      FRAMELOG("[VDP1] Write VBE=%d line = %d", (Vdp1Regs->TVMR >> 3) & 0x01, yabsys.LineCount);
    break;
    case 0x2:
      FRAMELOG("[VDP1] Write FCM=%d FCT=%d line = %d", (val & 0x02) >> 1, (val & 0x01), yabsys.LineCount);
      Vdp1Regs->FBCR = val;
      if ((Vdp1Regs->FBCR & 3) == 3) {
        //FRAMELOG("[VDP1] FCBR swtich to manual change");
        Vdp1External.manualchange = 1;
      }
      else if ((Vdp1Regs->FBCR & 3) == 2) {
        //FRAMELOG("[VDP1] FBCR swtich to manual erase");
        Vdp1External.manualerase = 1;
      }
      break;
    case 0x4:
      FRAMELOG("[VDP1] Write PTMR %X line = %d @ %08X", val, yabsys.LineCount, CurrentSH2->regs.PC);
      //Vdp1Regs->COPR = 0;
      //printf("COPR = 0 at %d\n", __LINE__);
      Vdp1Regs->PTMR = val;
#if YAB_ASYNC_RENDERING
      if (val == 1){ 
        g_ptmr_this_frame = 1;   /* VOUT dispatch ordering, see Vdp2HBlankOUT */
        FRAMELOG("[VDP1] VDPEV_DIRECT_DRAW %d/%d", YaGetQueueSize(vdp1_rcv_evqueue), yabsys.LineCount);
        {
          { extern void Vdp1JoinPendingRendersC(void); Vdp1JoinPendingRendersC(); }
          if ( YaGetQueueSize(vdp1_rcv_evqueue) > 0){
            yabsys.wait_line_count = -1;
            do{
              YabWaitEventQueue(vdp1_rcv_evqueue);
            } while (YaGetQueueSize(vdp1_rcv_evqueue) != 0);
          }
        }
        Vdp1Regs->EDSR >>= 1;
        yabsys.wait_line_count = yabsys.LineCount + 50;
        yabsys.wait_line_count %= yabsys.MaxLineCount;
        if (yabsys.wait_line_count == 5) { yabsys.wait_line_count = 4; }
        FRAMELOG("SET DIRECT WAIT %d", yabsys.wait_line_count);
        /* Take the snapshot here as well; no render is active at this point, so a
           single buffer is sufficient as in the VBlank-OUT path. */
        Vdp1RamSnapshot();
        YabAddEventQueue(evqueue,VDPEV_DIRECT_DRAW);
        Vdp1MarkRenderDispatched();
        YabThreadYield();
      }
#else
    if (val == 1){
      FRAMELOG("VDP1: VDPEV_DIRECT_DRAW\n");
        Vdp1Regs->EDSR >>= 1;
        Vdp1Draw(); 
        VIDCore->Vdp1DrawEnd();
        yabsys.wait_line_count = yabsys.LineCount + 50;
        yabsys.wait_line_count %= yabsys.MaxLineCount;
        //if (yabsys.wait_line_count == 2) { yabsys.wait_line_count = 3; } // it should not be the same line with render.
        FRAMELOG("VDP1: end line is %d", yabsys.wait_line_count);
    }
#endif
         break;
      case 0x6:
         Vdp1Regs->EWDR = val;
         break;
      case 0x8:
         Vdp1Regs->EWLR = val;
         break;
      case 0xA:
         Vdp1Regs->EWRR = val;
         break;
      case 0xC:
         Vdp1Regs->ENDR = val;
         Vdp1External.status = VDP1_STATUS_IDLE;
         yabsys.wait_line_count = -1;
         LOG("Force to stop Vdp1\n", addr);
         break;
      default:
         LOG("trying to write a Vdp1 read-only register - %08X\n", addr);
   }
}

//////////////////////////////////////////////////////////////////////////////

extern "C" void FASTCALL Vdp1WriteLong(u32 addr, UNUSED u32 val) {
   addr &= 0xFF;
   LOG("trying to long-write a Vdp1 register - %08X\n", addr);
}

//////////////////////////////////////////////////////////////////////////////

extern "C" void Vdp1DrawCommands(u8 * ram, Vdp1 * regs, u8* back_framebuffer)
{
  std::unique_lock<std::mutex> lk(vdp1_clock_mtx);
  LOG("VDP1: DrawCommands - %08X\n", regs->addr);
  regs->COPR = regs->addr >> 3;
  if (regs->addr > 0x7FFFF) {
    Vdp1External.status = VDP1_STATUS_IDLE;
    LOG("VDP1: Address error - %08X\n", regs->addr);
    return; // address error
  }
   u16 command = T1ReadWord(ram, regs->addr );
   if (command & 0x8000) {
     Vdp1External.status = VDP1_STATUS_IDLE;
     LOG("VDP1: Imidiate Finish - %08X\n", regs->addr);
     return;
   }

   int command_count = 0;
   u32 returnAddr = 0xffffffff;
   int empty_run = 0;

   while (!(command & 0x8000) && command_count < 4096) { // fix me
      regs->COPR = regs->addr >> 3;
      u32 to = 0;
#if 0
#if defined(YAB_ASYNC_RENDERING)
      vdp1_clock_cv.wait_for(lk, chrono::milliseconds(1000), [] { return (vdp1_clock > 0); });
#else
      if (vdp1_clock <= 0) {
        return;
      }
#endif
      vdp1_clock -= 10;
#endif
      /* Sprite types 2-7 without sprite window: if SDCTL[5:0] is 0 no layer
         accepts shadow, so a command whose every pixel carries the shadow bit
         (CMDPMOD "MSB on") has no visible effect on hardware. Drawing it
         anyway turned the shadow marker black under Gouraud shading
         (Batsugun: dark band across the middle of the screen). Skip it like
         a command with the skip bit set; the command chain is unchanged. */
      if (!(command & 0x4000) && (command & 0xF) <= 7 && (command & 0xF) != 5 && (command & 0xF) != 6 &&
          (T1ReadWord(ram, regs->addr + 4) & 0x8000) && Vdp2Regs != NULL) {
         const u16 spctl = Vdp2Regs->SPCTL, sptype = spctl & 0xF;
         if (sptype >= 2 && sptype < 8 && !(spctl & 0x10) && (Vdp2Regs->SDCTL & 0x3F) == 0)
            command |= 0x4000;
      }

      // First, process the command
      if (!(command & 0x4000)) { // if (!skip)
         switch (command & 0x000F) {
         case 0: // normal sprite draw
            g_v1_drawn++;
            VIDCore->Vdp1NormalSpriteDraw(ram, regs, back_framebuffer);
            break;
         case 1: // scaled sprite draw
            g_v1_drawn++;
            VIDCore->Vdp1ScaledSpriteDraw(ram, regs, back_framebuffer);
            break;
         case 2: // distorted sprite draw
         case 3: /* this one should be invalid, but some games
                 (Hardcore 4x4 for instance) use it instead of 2 */
            g_v1_drawn++;
            VIDCore->Vdp1DistortedSpriteDraw(ram, regs, back_framebuffer);
            break;
         case 4: // polygon draw
            g_v1_drawn++;
            VIDCore->Vdp1PolygonDraw(ram, regs, back_framebuffer);
            break;
         case 5: // polyline draw
         case 7: // undocumented mirror
            g_v1_drawn++;
            VIDCore->Vdp1PolylineDraw(ram, regs, back_framebuffer);
            break;
         case 6: // line draw
            g_v1_drawn++;
            VIDCore->Vdp1LineDraw(ram, regs, back_framebuffer);
            break;
         case 8: // user clipping coordinates
         case 11: // undocumented mirror
            VIDCore->Vdp1UserClipping(ram, regs);
            break;
         case 9: // system clipping coordinates
            VIDCore->Vdp1SystemClipping(ram, regs);
            break;
         case 10: // local coordinate
            VIDCore->Vdp1LocalCoordinate(ram, regs);
            break;
         default: // Abort
            VDP1LOG("VDP1: Bad command: %x\n", command);
            regs->EDSR |= 2;
            regs->LOPR = regs->addr >> 3;
            regs->COPR = regs->addr >> 3;
            Vdp1External.status = VDP1_STATUS_IDLE;
            return;
         }
      }

	  /* An own-command error (BATSUGUN) aborts in the default branch of the command loop above. */

      // Next, determine where to go next
      switch ((command & 0x3000) >> 12) {
      case 0: // NEXT, jump to following table
         regs->addr += 0x20;
         break;
      case 1: // ASSIGN, jump to CMDLINK
         regs->addr = T1ReadWord(ram, regs->addr + 2) * 8;

         // Badd adress. it causes infinity loop 
         if (regs->addr == 0) {
           LOG("VDP1: BAD jump to 0, forced to finish");
           Vdp1External.status = VDP1_STATUS_IDLE;
           return;
         }

         break;
      case 2: // CALL, call a subroutine
         if (returnAddr == 0xFFFFFFFF)
            returnAddr = regs->addr + 0x20;
         regs->addr = T1ReadWord(ram, regs->addr + 2) * 8;
         // Badd adress. it causes infinity loop 
         if (regs->addr == 0) {
           LOG("VDP1: BAD jump to 0, forced to finish");
           Vdp1External.status = VDP1_STATUS_IDLE;
           return;
         }
         break;
      case 3: // RETURN, return from subroutine
         if (returnAddr != 0xFFFFFFFF) {
            regs->addr = returnAddr;
            returnAddr = 0xFFFFFFFF;
         }
         else
            regs->addr += 0x20;
         // Badd adress. it causes infinity loop 
         if (regs->addr == 0) {
           LOG("VDP1: BAD jump to 0, forced to finish");
           Vdp1External.status = VDP1_STATUS_IDLE;
           return;
         }
         break;
      }

      command = T1ReadWord(ram, regs->addr & 0x7FFFF);
      command_count++;
      /* Four completely empty command slots in a row mean the table has run
         on into unused VDP1 RAM. Real hardware never gets that far within
         its drawing time, but without a VDP1 cycle budget the emulator would go on
         and draw the random data behind it (Batsugun: noise and a dark band
         in the top part of the screen). Treat it as the end of the list.
         A single empty slot is legal (Time Bokan starts its list with one). */
      if (!(command & 0x8000)) {
         int empty = 1;
         for (int k = 0; k < 32; k += 2)
            if (T1ReadWord(ram, (regs->addr + k) & 0x7FFFF)) { empty = 0; break; }
         empty_run = empty ? empty_run + 1 : 0;
         if (empty_run >= 4)
            command |= 0x8000;
      }
      if (command & 0x8000) {
        LOG("VDP1: Command Finished! count = %d @ %08X", command_count, regs->addr);
        Vdp1External.status = VDP1_STATUS_IDLE;
        /* After the end of the command list LOPR and COPR must hold the end address
           (needed by SYOUJYOKAKUMEI UTENA, which polls them). */
        regs->LOPR = regs->addr >> 3;
        regs->COPR = regs->addr >> 3;
      }
   }

   if (Vdp1External.status == VDP1_STATUS_RUNNING) {
     Vdp1External.status = VDP1_STATUS_IDLE;
     LOG("VDP1: Readched to max comand count = %d", command_count);
   }
}

//ensure that registers are set correctly 
extern "C" void Vdp1FakeDrawCommands(u8 * ram, Vdp1 * regs)
{
   u16 command = T1ReadWord(ram, regs->addr);
   u32 commandCounter = 0;
   u32 returnAddr = 0xffffffff;

   while (!(command & 0x8000) && commandCounter < 2000) { // fix me
      // First, process the command
      if (!(command & 0x4000)) { // if (!skip)
         switch (command & 0x000F) {
         case 0: // normal sprite draw
         case 1: // scaled sprite draw
         case 2: // distorted sprite draw
         case 3: /* this one should be invalid, but some games
                 (Hardcore 4x4 for instance) use it instead of 2 */
         case 4: // polygon draw
         case 5: // polyline draw
         case 6: // line draw
         case 7: // undocumented polyline draw mirror
            break;
         case 8: // user clipping coordinates
         case 11: // undocumented mirror
            VIDCore->Vdp1UserClipping(ram, regs);
            break;
         case 9: // system clipping coordinates
            VIDCore->Vdp1SystemClipping(ram, regs);
            break;
         case 10: // local coordinate
            VIDCore->Vdp1LocalCoordinate(ram, regs);
            break;
         default: // Abort
            VDP1LOG("vdp1\t: Bad command: %x\n", command);
            regs->EDSR |= 2;
            regs->LOPR = regs->addr >> 3;
            regs->COPR = regs->addr >> 3;
            return;
         }
      }

      // Next, determine where to go next
      switch ((command & 0x3000) >> 12) {
      case 0: // NEXT, jump to following table
         regs->addr += 0x20;
         break;
      case 1: // ASSIGN, jump to CMDLINK
         regs->addr = T1ReadWord(ram, regs->addr + 2) * 8;
         break;
      case 2: // CALL, call a subroutine
         if (returnAddr == 0xFFFFFFFF)
            returnAddr = regs->addr + 0x20;

         regs->addr = T1ReadWord(ram, regs->addr + 2) * 8;
         break;
      case 3: // RETURN, return from subroutine
         if (returnAddr != 0xFFFFFFFF) {
            regs->addr = returnAddr;
            returnAddr = 0xFFFFFFFF;
         }
         else
            regs->addr += 0x20;
         break;
      }

      command = T1ReadWord(ram, regs->addr);
      commandCounter++;
   }
}


int Vdp1GenerateCCode() {

  FILE * regfp = fopen("v1reg.c", "w");
  fprintf(regfp, "short v1reg[] = { \n");
    fprintf(regfp, "0x%04X,\n", Vdp1Regs->TVMR);
    fprintf(regfp, "0x%04X,\n", Vdp1Regs->FBCR);
    fprintf(regfp, "0x%04X,\n", Vdp1Regs->PTMR);
    fprintf(regfp, "0x%04X,\n", Vdp1Regs->EWDR);
    fprintf(regfp, "0x%04X,\n", Vdp1Regs->EWLR);
    fprintf(regfp, "0x%04X\n", Vdp1Regs->ENDR);
  fprintf(regfp, "};\n");
  fclose(regfp);

  FILE * ramfp = fopen("v1ram.c", "w");
  fprintf(ramfp, "short v1ram[] = { \n");
  for (int i = 0; i < 0x80000; i+=2) {
    u16 data = Vdp1RamReadWord(i);
    fprintf(ramfp, "0x%04X", data);
    if (i != 0 && (i % 16) == 0) {
      fprintf(ramfp, ",\n");
    }
    else {
      fprintf(ramfp, ",");
    }
  }
  fprintf(ramfp, "};\n");
  fclose(ramfp);

  return 0;
}

#if _DEBUG
int g_vdp1_debug_dmp = 0;
#endif

extern "C" void Vdp1Draw(void)
{
#if _DEBUG
  if (g_vdp1_debug_dmp == 1) {
    Vdp1GenerateCCode();
    g_vdp1_debug_dmp = 0;
    vdp2ReqDump();
  }
#endif

  FRAMELOG("Vdp1Draw");
   if (!Vdp1External.disptoggle)
   {
      Vdp1NoDraw();
      return;
   }

   if (Vdp1External.status == VDP1_STATUS_IDLE) {
     Vdp1Regs->addr = 0;
     // beginning of a frame
     // BEF <- CEF
     // CEF <- 0
     //Vdp1Regs->EDSR >>= 1;
     /* this should be done after a frame change or a plot trigger */
     Vdp1Regs->COPR = 0;
     //printf("COPR = %d at %d\n", Vdp1Regs->COPR, __LINE__);
   }

   Vdp1External.status = VDP1_STATUS_RUNNING;
   VIDCore->Vdp1DrawStart();

   //printf("COPR = %d at %d\n", Vdp1Regs->COPR, __LINE__);

   //VIDCore->Vdp1DrawEnd();

   // we set two bits to 1
   //Vdp1Regs->EDSR |= 2;
   //Vdp1Regs->COPR = Vdp1Regs->addr >> 3;
   //ScuSendDrawEnd();
   //FRAMELOG("Vdp1Draw end at %d line EDSR=%02X", yabsys.LineCount, Vdp1Regs->EDSR);

}

//////////////////////////////////////////////////////////////////////////////

extern "C" void Vdp1NoDraw(void) {
   // beginning of a frame (ST-013-R3-061694 page 53)
   // BEF <- CEF
   // CEF <- 0
   //Vdp1Regs->EDSR >>= 1;
   /* this should be done after a frame change or a plot trigger */
   Vdp1Regs->COPR = 0;

   Vdp1FakeDrawCommands(Vdp1Ram, Vdp1Regs);

   // we set two bits to 1
   Vdp1Regs->EDSR |= 2;
   ScuSendDrawEnd();
}

//////////////////////////////////////////////////////////////////////////////

extern "C" void FASTCALL Vdp1ReadCommand(vdp1cmd_struct *cmd, u32 addr, u8* ram) {
  addr &= 0x7FFFF;
   cmd->CMDCTRL = T1ReadWord(ram, addr);
   cmd->CMDLINK = T1ReadWord(ram, addr + 0x2);
   cmd->CMDPMOD = T1ReadWord(ram, addr + 0x4);
   cmd->CMDCOLR = T1ReadWord(ram, addr + 0x6);
   cmd->CMDSRCA = T1ReadWord(ram, addr + 0x8);
   cmd->CMDSIZE = T1ReadWord(ram, addr + 0xA);
   cmd->CMDXA = T1ReadWord(ram, addr + 0xC);
   cmd->CMDYA = T1ReadWord(ram, addr + 0xE);
   cmd->CMDXB = T1ReadWord(ram, addr + 0x10);
   cmd->CMDYB = T1ReadWord(ram, addr + 0x12);
   cmd->CMDXC = T1ReadWord(ram, addr + 0x14);
   cmd->CMDYC = T1ReadWord(ram, addr + 0x16);
   cmd->CMDXD = T1ReadWord(ram, addr + 0x18);
   cmd->CMDYD = T1ReadWord(ram, addr + 0x1A);
   cmd->CMDGRDA = T1ReadWord(ram, addr + 0x1C);
}

//////////////////////////////////////////////////////////////////////////////

extern "C" int Vdp1SaveState(FILE *fp)
{
   int offset;
   IOCheck_struct check = { 0, 0 };
#ifdef IMPROVED_SAVESTATES
   int i = 0;
   u16 back_framebuffer[0x20000] = { 0 };
#endif

   offset = StateWriteHeader(fp, "VDP1", 1);

   // Write registers
   ywrite(&check, (void *)Vdp1Regs, sizeof(Vdp1), 1, fp);

   // Write VDP1 ram
   ywrite(&check, (void *)Vdp1Ram, 0x80000, 1, fp);

#ifdef IMPROVED_SAVESTATES

   void(*Vdp1ReadFrameBuffer)(u32 type, u32 addr, void * out) = VIDCore->Vdp1ReadFrameBuffer;
   void(*Vdp1WriteFrameBuffer)(u32 type, u32 addr, u32 val) = VIDCore->Vdp1WriteFrameBuffer;

   VIDCore->Vdp1ReadFrameBuffer = NULL;
   VIDCore->Vdp1WriteFrameBuffer = NULL;

   for (i = 0; i < 0x20000; i++)
      back_framebuffer[i] = Vdp1FrameBufferReadWord(i<<1);

   VIDCore->Vdp1ReadFrameBuffer = Vdp1ReadFrameBuffer;
   VIDCore->Vdp1WriteFrameBuffer = Vdp1WriteFrameBuffer;

   ywrite(&check, (void *)back_framebuffer, 0x40000, 1, fp);
#endif
   return StateFinishHeader(fp, offset);
}

//////////////////////////////////////////////////////////////////////////////

extern "C" int Vdp1LoadState(FILE *fp, UNUSED int version, int size)
{
   IOCheck_struct check = { 0, 0 };
#ifdef IMPROVED_SAVESTATES
   int i = 0;
   u16 back_framebuffer[0x20000] = { 0 };
#endif

   // Read registers
   yread(&check, (void *)Vdp1Regs, sizeof(Vdp1), 1, fp);

   // Read VDP1 ram
   yread(&check, (void *)Vdp1Ram, 0x80000, 1, fp);
   /* VRAM was loaded bypassing the write handlers, so flush the page stamps and resnapshot. */
   Vdp1TexStampFlush();
  Vdp1RamSnapAllNew();

#ifdef IMPROVED_SAVESTATES

   void(*Vdp1ReadFrameBuffer)(u32 type, u32 addr, void * out) = VIDCore->Vdp1ReadFrameBuffer;
   void(*Vdp1WriteFrameBuffer)(u32 type, u32 addr, u32 val) = VIDCore->Vdp1WriteFrameBuffer;

   VIDCore->Vdp1ReadFrameBuffer = NULL;
   VIDCore->Vdp1WriteFrameBuffer = NULL;

   yread(&check, (void *)back_framebuffer, 0x40000, 1, fp);

   for (i = 0; i < 0x20000; i++)
      Vdp1FrameBufferWriteWord(i<<1, back_framebuffer[i]);

   VIDCore->Vdp1ReadFrameBuffer = Vdp1ReadFrameBuffer;
   VIDCore->Vdp1WriteFrameBuffer = Vdp1WriteFrameBuffer;

#endif
   return size;
}

//////////////////////////////////////////////////////////////////////////////

u32 Vdp1DebugGetCommandNumberAddr(u32 number)
{
   u32 addr = 0;
   u32 returnAddr = 0xFFFFFFFF;
   u32 commandCounter = 0;
   u16 command;

   command = T1ReadWord(Vdp1Ram, addr);

   while (!(command & 0x8000) && commandCounter != number)
   {
      // Make sure we're still dealing with a valid command
      if ((command & 0x000C) == 0x000C)
         // Invalid, abort
         return 0xFFFFFFFF;

      // Determine where to go next
      switch ((command & 0x3000) >> 12)
      {
         case 0: // NEXT, jump to following table
            addr += 0x20;
            break;
         case 1: // ASSIGN, jump to CMDLINK
            addr = T1ReadWord(Vdp1Ram, addr + 2) * 8;
            break;
         case 2: // CALL, call a subroutine
            if (returnAddr == 0xFFFFFFFF)
               returnAddr = addr + 0x20;
	
            addr = T1ReadWord(Vdp1Ram, addr + 2) * 8;
            break;
         case 3: // RETURN, return from subroutine
            if (returnAddr != 0xFFFFFFFF) {
               addr = returnAddr;
               returnAddr = 0xFFFFFFFF;
            }
            else
               addr += 0x20;
            break;
      }

      if (addr > 0x7FFE0)
         return 0xFFFFFFFF;
      command = T1ReadWord(Vdp1Ram, addr);
      commandCounter++;    
   }

   if (commandCounter == number)
      return addr;
   else
      return 0xFFFFFFFF;
}

//////////////////////////////////////////////////////////////////////////////

char *Vdp1DebugGetCommandNumberName(u32 number)
{
   u32 addr;
   u16 command;

   if ((addr = Vdp1DebugGetCommandNumberAddr(number)) != 0xFFFFFFFF)
   {
      command = T1ReadWord(Vdp1Ram, addr);

      if (command & 0x8000)
         return "Draw End";

      // Figure out command name
      switch (command & 0x000F)
      {
         case 0:
            return "Normal Sprite";
         case 1:
            return "Scaled Sprite";
         case 2:
            return "Distorted Sprite";
         case 3:
            return "Distorted Sprite *";
         case 4:
            return "Polygon";
         case 5:
            return "Polyline";
         case 6:
            return "Line";
         case 7:
            return "Polyline *";
         case 8:
            return "User Clipping Coordinates";
         case 9:
            return "System Clipping Coordinates";
         case 10:
            return "Local Coordinates";
         case 11:
            return "User Clipping Coordinates *";
         default:
             return "Bad command";
      }
   }
   else
      return NULL;
}

//////////////////////////////////////////////////////////////////////////////

void Vdp1DebugCommand(u32 number, char *outstring)
{
   u16 command;
   vdp1cmd_struct cmd;
   u32 addr;

   if ((addr = Vdp1DebugGetCommandNumberAddr(number)) == 0xFFFFFFFF)
      return;

   command = T1ReadWord(Vdp1Ram, addr);

   if (command & 0x8000)
   {
      // Draw End
      outstring[0] = 0x00;
      return;
   }

   if (command & 0x4000)
   {
      AddString(outstring, "Command is skipped\r\n");
      return;
   }

   Vdp1ReadCommand(&cmd, addr, Vdp1Ram);

   switch (cmd.CMDCTRL & 0x000F)
   {
      case 0:
         AddString(outstring, "Normal Sprite\r\n");
         AddString(outstring, "x = %d, y = %d\r\n", cmd.CMDXA, cmd.CMDYA);
         break;
      case 1:
         AddString(outstring, "Scaled Sprite\r\n");

         AddString(outstring, "Zoom Point: ");

         switch ((cmd.CMDCTRL >> 8) & 0xF)
         {
            case 0x0:
               AddString(outstring, "Only two coordinates\r\n");
               break;
            case 0x5:
               AddString(outstring, "Upper-left\r\n");
               break;
            case 0x6:
               AddString(outstring, "Upper-center\r\n");
               break;
            case 0x7:
               AddString(outstring, "Upper-right\r\n");
               break;
            case 0x9:
               AddString(outstring, "Center-left\r\n");
               break;
            case 0xA:
               AddString(outstring, "Center-center\r\n");
               break;
            case 0xB:
               AddString(outstring, "Center-right\r\n");
               break;
            case 0xC:
               AddString(outstring, "Lower-left\r\n");
               break;
            case 0xE:
               AddString(outstring, "Lower-center\r\n");
               break;
            case 0xF:
               AddString(outstring, "Lower-right\r\n");
               break;
            default: break;
         }

         if (((cmd.CMDCTRL >> 8) & 0xF) == 0)
         {
            AddString(outstring, "xa = %d, ya = %d, xc = %d, yc = %d\r\n", cmd.CMDXA, cmd.CMDYA, cmd.CMDXC, cmd.CMDYC);
         }
         else
         {
            AddString(outstring, "xa = %d, ya = %d, xb = %d, yb = %d\r\n", cmd.CMDXA, cmd.CMDYA, cmd.CMDXB, cmd.CMDYB);
         }

         break;
      case 2:
         AddString(outstring, "Distorted Sprite\r\n");
         AddString(outstring, "x1 = %d, y1 = %d, x2 = %d, y2 = %d\r\n", cmd.CMDXA, cmd.CMDYA, cmd.CMDXB, cmd.CMDYB);
         AddString(outstring, "x3 = %d, y3 = %d, x4 = %d, y4 = %d\r\n", cmd.CMDXC, cmd.CMDYC, cmd.CMDXD, cmd.CMDYD);
         break;
      case 3:
         AddString(outstring, "Distorted Sprite *\r\n");
         AddString(outstring, "x1 = %d, y1 = %d, x2 = %d, y2 = %d\r\n", cmd.CMDXA, cmd.CMDYA, cmd.CMDXB, cmd.CMDYB);
         AddString(outstring, "x3 = %d, y3 = %d, x4 = %d, y4 = %d\r\n", cmd.CMDXC, cmd.CMDYC, cmd.CMDXD, cmd.CMDYD);
         break;
      case 4:
         AddString(outstring, "Polygon\r\n");
         AddString(outstring, "x1 = %d, y1 = %d, x2 = %d, y2 = %d\r\n", cmd.CMDXA, cmd.CMDYA, cmd.CMDXB, cmd.CMDYB);
         AddString(outstring, "x3 = %d, y3 = %d, x4 = %d, y4 = %d\r\n", cmd.CMDXC, cmd.CMDYC, cmd.CMDXD, cmd.CMDYD);
         break;
      case 5:
         AddString(outstring, "Polyline\r\n");
         AddString(outstring, "x1 = %d, y1 = %d, x2 = %d, y2 = %d\r\n", cmd.CMDXA, cmd.CMDYA, cmd.CMDXB, cmd.CMDYB);
         AddString(outstring, "x3 = %d, y3 = %d, x4 = %d, y4 = %d\r\n", cmd.CMDXC, cmd.CMDYC, cmd.CMDXD, cmd.CMDYD);
         break;
      case 6:
         AddString(outstring, "Line\r\n");
         AddString(outstring, "x1 = %d, y1 = %d, x2 = %d, y2 = %d\r\n", cmd.CMDXA, cmd.CMDYA, cmd.CMDXB, cmd.CMDYB);
         break;
      case 7:
         AddString(outstring, "Polyline *\r\n");
         AddString(outstring, "x1 = %d, y1 = %d, x2 = %d, y2 = %d\r\n", cmd.CMDXA, cmd.CMDYA, cmd.CMDXB, cmd.CMDYB);
         AddString(outstring, "x3 = %d, y3 = %d, x4 = %d, y4 = %d\r\n", cmd.CMDXC, cmd.CMDYC, cmd.CMDXD, cmd.CMDYD);
         break;
      case 8:
         AddString(outstring, "User Clipping\r\n");
         AddString(outstring, "x1 = %d, y1 = %d, x2 = %d, y2 = %d\r\n", cmd.CMDXA, cmd.CMDYA, cmd.CMDXC, cmd.CMDYC);
         break;
      case 9:
         AddString(outstring, "System Clipping\r\n");
         AddString(outstring, "x1 = 0, y1 = 0, x2 = %d, y2 = %d\r\n", cmd.CMDXC, cmd.CMDYC);
         break;
      case 10:
         AddString(outstring, "Local Coordinates\r\n");
         AddString(outstring, "x = %d, y = %d\r\n", cmd.CMDXA, cmd.CMDYA);
         break;
      default:
         AddString(outstring, "Invalid command\r\n");
         return;
   }

   // Only Sprite commands use CMDSRCA, CMDSIZE
   if (!(cmd.CMDCTRL & 0x000C))
   {
      AddString(outstring, "Texture address = %08X\r\n", ((unsigned int)cmd.CMDSRCA) << 3);
      AddString(outstring, "Texture width = %d, height = %d\r\n", (cmd.CMDSIZE & 0x3F00) >> 5, cmd.CMDSIZE & 0xFF);
      AddString(outstring, "Texture read direction: ");

      switch ((cmd.CMDCTRL >> 4) & 0x3)
      {
         case 0:
            AddString(outstring, "Normal\r\n");
            break;
         case 1:
            AddString(outstring, "Reversed horizontal\r\n");
            break;
         case 2:
            AddString(outstring, "Reversed vertical\r\n");
            break;
         case 3:
            AddString(outstring, "Reversed horizontal and vertical\r\n");
            break;
         default: break;
      }      
   }

   // Only draw commands use CMDPMOD
   if (!(cmd.CMDCTRL & 0x0008))
   {
      if (cmd.CMDPMOD & 0x8000)
      {
         AddString(outstring, "MSB set\r\n");
      }

      if (cmd.CMDPMOD & 0x1000)
      {
         AddString(outstring, "High Speed Shrink Enabled\r\n");
      }

      if (!(cmd.CMDPMOD & 0x0800))
      {
         AddString(outstring, "Pre-clipping Enabled\r\n");
      }

      if (cmd.CMDPMOD & 0x0400)
      {
         AddString(outstring, "User Clipping Enabled\r\n");
         AddString(outstring, "Clipping Mode = %d\r\n", (cmd.CMDPMOD >> 9) & 0x1);
      }

      if (cmd.CMDPMOD & 0x0100)
      {
         AddString(outstring, "Mesh Enabled\r\n");
      }

      if (!(cmd.CMDPMOD & 0x0080))
      {
         AddString(outstring, "End Code Enabled\r\n");
      }

      if (!(cmd.CMDPMOD & 0x0040))
      {
         AddString(outstring, "Transparent Pixel Enabled\r\n");
      }

      AddString(outstring, "Color mode: ");

      switch ((cmd.CMDPMOD >> 3) & 0x7)
      {
         case 0:
            AddString(outstring, "4 BPP(16 color bank)\r\n");
            AddString(outstring, "Color bank: %08X\r\n", (cmd.CMDCOLR << 3));
            break;
         case 1:
            AddString(outstring, "4 BPP(16 color LUT)\r\n");
            AddString(outstring, "Color lookup table: %08X\r\n", (cmd.CMDCOLR << 3));
            break;
         case 2:
            AddString(outstring, "8 BPP(64 color bank)\r\n");
            AddString(outstring, "Color bank: %08X\r\n", (cmd.CMDCOLR << 3));
            break;
         case 3:
            AddString(outstring, "8 BPP(128 color bank)\r\n");
            AddString(outstring, "Color bank: %08X\r\n", (cmd.CMDCOLR << 3));
            break;
         case 4:
            AddString(outstring, "8 BPP(256 color bank)\r\n");
            AddString(outstring, "Color bank: %08X\r\n", (cmd.CMDCOLR << 3));
            break;
         case 5:
            AddString(outstring, "15 BPP(RGB)\r\n");

            // Only non-textured commands
            if (cmd.CMDCTRL & 0x0004)
            {
               AddString(outstring, "Non-textured color: %04X\r\n", cmd.CMDCOLR);
            }
            break;
         default: break;
      }

      AddString(outstring, "Color Calc. mode: ");

      switch (cmd.CMDPMOD & 0x7)
      {
         case 0:
            AddString(outstring, "Replace\r\n");
            break;
         case 1:
            AddString(outstring, "Cannot overwrite/Shadow\r\n");
            break;
         case 2:
            AddString(outstring, "Half-luminance\r\n");
            break;
         case 3:
            AddString(outstring, "Replace/Half-transparent\r\n");
            break;
         case 4:
            AddString(outstring, "Gouraud Shading\r\n");
            AddString(outstring, "Gouraud Shading Table = %08X\r\n", ((unsigned int)cmd.CMDGRDA) << 3);
            break;
         case 6:
            AddString(outstring, "Gouraud Shading + Half-luminance\r\n");
            AddString(outstring, "Gouraud Shading Table = %08X\r\n", ((unsigned int)cmd.CMDGRDA) << 3);
            break;
         case 7:
            AddString(outstring, "Gouraud Shading/Gouraud Shading + Half-transparent\r\n");
            AddString(outstring, "Gouraud Shading Table = %08X\r\n", ((unsigned int)cmd.CMDGRDA) << 3);
            break;
         default: break;
      }
   }
}

//////////////////////////////////////////////////////////////////////////////

#if defined WORDS_BIGENDIAN
#define SAT2YAB1(alpha,temp)		(alpha | (temp & 0x7C00) << 1 | (temp & 0x3E0) << 14 | (temp & 0x1F) << 27)
#else
#define SAT2YAB1(alpha,temp)		(alpha << 24 | (temp & 0x1F) << 3 | (temp & 0x3E0) << 6 | (temp & 0x7C00) << 9)
#endif

#if defined WORDS_BIGENDIAN
#define SAT2YAB2(alpha,dot1,dot2)       (((dot2 & 0xFF) << 24) | ((dot2 & 0xFF00) << 8) | ((dot1 & 0xFF) << 8) | alpha)
#else
#define SAT2YAB2(alpha,dot1,dot2)       (alpha << 24 | ((dot1 & 0xFF) << 16) | (dot2 & 0xFF00) | (dot2 & 0xFF))
#endif

static u32 ColorRamGetColor(u32 colorindex)
{
   switch(Vdp2Internal.ColorMode)
   {
      case 0:
      case 1:
      {
         u32 tmp;
         colorindex <<= 1;
         tmp = T2ReadWord(Vdp2ColorRam, colorindex & 0xFFF);
         return SAT2YAB1(0xFF, tmp);
      }
      case 2:
      {
         u32 tmp1, tmp2;
         colorindex <<= 2;
         colorindex &= 0xFFF;
         tmp1 = T2ReadWord(Vdp2ColorRam, colorindex);
         tmp2 = T2ReadWord(Vdp2ColorRam, colorindex+2);
         return SAT2YAB2(0xFF, tmp1, tmp2);
      }
      default: break;
   }

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

static INLINE int CheckEndcode(int dot, int endcode, int *code)
{
   if (dot == endcode)
   {
      code[0]++;
      if (code[0] == 2)
      {
         code[0] = 0;
         return 2;
      }
      return 1;
   }

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

static INLINE int DoEndcode(int count, u32 *charAddr, u32 **textdata, int width, int xoff, int oddpixel, int pixelsize)
{
   if (count > 1)
   {
      float divisor = (float)(8 / pixelsize);

      if(divisor != 0)
         charAddr[0] += (int)((float)(width - xoff + oddpixel) / divisor);
      memset(textdata[0], 0, sizeof(u32) * (width - xoff));
      textdata[0] += (width - xoff);
      return 1;
   }
   else
      *textdata[0]++ = 0;

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

u32 *Vdp1DebugTexture(u32 number, int *w, int *h)
{
   u16 command;
   vdp1cmd_struct cmd;
   u32 addr;
   u32 *texture;
   u32 charAddr;
   u32 dot;
   u8 SPD;
   u32 alpha;
   u32 *textdata;   
   int isendcode=0;
   int code=0;
   int ret;

   if ((addr = Vdp1DebugGetCommandNumberAddr(number)) == 0xFFFFFFFF)
      return NULL;

   command = T1ReadWord(Vdp1Ram, addr);

   if (command & 0x8000)
      // Draw End
      return NULL;

   if (command & 0x4000)
      // Command Skipped
      return NULL;

   Vdp1ReadCommand(&cmd, addr, Vdp1Ram);

   switch (cmd.CMDCTRL & 0x000F)
   {
      case 0: // Normal Sprite
      case 1: // Scaled Sprite
      case 2: // Distorted Sprite
      case 3: // Distorted Sprite *
         w[0] = (cmd.CMDSIZE & 0x3F00) >> 5;
         h[0] = cmd.CMDSIZE & 0xFF;

         if ((texture = (u32 *)malloc(sizeof(u32) * w[0] * h[0])) == NULL)
            return NULL;

         if (!(cmd.CMDPMOD & 0x80))
         {
            isendcode = 1;
            code = 0;
         }
         else
            isendcode = 0;
         break;
      case 4: // Polygon
      case 5: // Polyline
      case 6: // Line
      case 7: // Polyline *
         // Do 1x1 pixel
         w[0] = 1;
         h[0] = 1;
         if ((texture = (u32 *)malloc(sizeof(u32))) == NULL)
            return NULL;

         if (cmd.CMDCOLR & 0x8000)
            texture[0] = SAT2YAB1(0xFF, cmd.CMDCOLR);
         else
            texture[0] = ColorRamGetColor(cmd.CMDCOLR);

         return texture;
      case 8: // User Clipping
      case 9: // System Clipping
      case 10: // Local Coordinates
      case 11: // User Clipping *
         return NULL;
      default: // Invalid command
         return NULL;
   }

   charAddr = cmd.CMDSRCA * 8;
   SPD = ((cmd.CMDPMOD & 0x40) != 0);
   alpha = 0xFF;
   textdata = texture;

   switch((cmd.CMDPMOD >> 3) & 0x7)
   {
      case 0:
      {
         // 4 bpp Bank mode
         u32 colorBank = cmd.CMDCOLR;
         u32 colorOffset = (Vdp2Regs->CRAOFB & 0x70) << 4;
         u16 i;

         for(i = 0;i < h[0];i++)
         {
            u16 j;
            j = 0;
            while(j < w[0])
            {
               dot = T1ReadByte(Vdp1Ram, charAddr & 0x7FFFF);

               // Pixel 1
               if (isendcode && (ret = CheckEndcode(dot >> 4, 0xF, &code)) > 0)
               {
                  if (DoEndcode(ret, &charAddr, &textdata, w[0], j, 0, 4))
                     break;
               }
               else
               {
                  if (((dot >> 4) == 0) && !SPD) *textdata++ = 0;
                  else *textdata++ = ColorRamGetColor(((dot >> 4) | colorBank) + colorOffset);
               }

               j += 1;

               // Pixel 2
               if (isendcode && (ret = CheckEndcode(dot & 0xF, 0xF, &code)) > 0)
               {
                  if (DoEndcode(ret, &charAddr, &textdata, w[0], j, 1, 4))
                     break;
               }
               else
               {
                  if (((dot & 0xF) == 0) && !SPD) *textdata++ = 0;
                  else *textdata++ = ColorRamGetColor(((dot & 0xF) | colorBank) + colorOffset);
               }

               j += 1;
               charAddr += 1;
            }
         }
         break;
      }
      case 1:
      {
         // 4 bpp LUT mode
         u32 temp;
         u32 colorLut = cmd.CMDCOLR * 8;
         u16 i;

         for(i = 0;i < h[0];i++)
         {
            u16 j;
            j = 0;
            while(j < w[0])
            {
               dot = T1ReadByte(Vdp1Ram, charAddr & 0x7FFFF);

               if (isendcode && (ret = CheckEndcode(dot >> 4, 0xF, &code)) > 0)
               {
                  if (DoEndcode(ret, &charAddr, &textdata, w[0], j, 0, 4))
                     break;
               }
               else
               {
                  if (((dot >> 4) == 0) && !SPD)
                     *textdata++ = 0;
                  else
                  {
                     temp = T1ReadWord(Vdp1Ram, ((dot >> 4) * 2 + colorLut) & 0x7FFFF);
                     if (temp & 0x8000)
                        *textdata++ = SAT2YAB1(0xFF, temp);
                     else
                        *textdata++ = ColorRamGetColor(temp);
                  }
               }

               j += 1;

               if (isendcode && (ret = CheckEndcode(dot & 0xF, 0xF, &code)) > 0)
               {
                  if (DoEndcode(ret, &charAddr, &textdata, w[0], j, 1, 4))
                     break;
               }
               else
               {
                  if (((dot & 0xF) == 0) && !SPD)
                     *textdata++ = 0;
                  else
                  {
                     temp = T1ReadWord(Vdp1Ram, ((dot & 0xF) * 2 + colorLut) & 0x7FFFF);
                     if (temp & 0x8000)
                        *textdata++ = SAT2YAB1(0xFF, temp);
                     else
                        *textdata++ = ColorRamGetColor(temp);
                  }
               }

               j += 1;

               charAddr += 1;
            }
         }
         break;
      }
      case 2:
      {
         // 8 bpp(64 color) Bank mode
         u32 colorBank = cmd.CMDCOLR;
         u32 colorOffset = (Vdp2Regs->CRAOFB & 0x70) << 4;

         u16 i, j;

         for(i = 0;i < h[0];i++)
         {
            for(j = 0;j < w[0];j++)
            {
               dot = T1ReadByte(Vdp1Ram, charAddr & 0x7FFFF) & 0x3F;
               charAddr++;

               if ((dot == 0) && !SPD) *textdata++ = 0;
               else *textdata++ = ColorRamGetColor((dot | colorBank) + colorOffset);
            }
         }
         break;
      }
      case 3:
      {
         // 8 bpp(128 color) Bank mode
         u32 colorBank = cmd.CMDCOLR;
         u32 colorOffset = (Vdp2Regs->CRAOFB & 0x70) << 4;
         u16 i, j;

         for(i = 0;i < h[0];i++)
         {
            for(j = 0;j < w[0];j++)
            {
               dot = T1ReadByte(Vdp1Ram, charAddr & 0x7FFFF) & 0x7F;
               charAddr++;

               if ((dot == 0) && !SPD) *textdata++ = 0;
               else *textdata++ = ColorRamGetColor((dot | colorBank) + colorOffset);
            }
         }
         break;
      }
      case 4:
      {
         // 8 bpp(256 color) Bank mode
         u32 colorBank = cmd.CMDCOLR;
         u32 colorOffset = (Vdp2Regs->CRAOFB & 0x70) << 4;
         u16 i, j;

         for(i = 0;i < h[0];i++)
         {
            for(j = 0;j < w[0];j++)
            {
               dot = T1ReadByte(Vdp1Ram, charAddr & 0x7FFFF);
               charAddr++;

               if ((dot == 0) && !SPD) *textdata++ = 0;
               else *textdata++ = ColorRamGetColor((dot | colorBank) + colorOffset);
            }
         }
         break;
      }
      case 5:
      {
         // 16 bpp Bank mode
         u16 i, j;

         for(i = 0;i < h[0];i++)
         {
            for(j = 0;j < w[0];j++)
            {
               dot = T1ReadWord(Vdp1Ram, charAddr & 0x7FFFF);

               if (isendcode && (ret = CheckEndcode(dot, 0x7FFF, &code)) > 0)
               {
                  if (DoEndcode(ret, &charAddr, &textdata, w[0], j, 0, 16))
                     break;
               }
               else
               {
                  //if (!(dot & 0x8000) && (Vdp2Regs->SPCTL & 0x20)) printf("mixed mode\n");
                  if (!(dot & 0x8000) && !SPD) *textdata++ = 0;
                  else *textdata++ = SAT2YAB1(0xFF, dot);
               }

               charAddr += 2;
            }
         }
         break;
      }
      default:
         break;
   }

   return texture;
}

//////////////////////////////////////////////////////////////////////////////

void ToggleVDP1(void)
{
   Vdp1External.disptoggle ^= 1;
}

//////////////////////////////////////////////////////////////////////////////
// Dummy Video Interface
//////////////////////////////////////////////////////////////////////////////
int VIDDummyInit(void);
void VIDDummyDeInit(void);
void VIDDummyResize(int, int, unsigned int, unsigned int, int, int );
int VIDDummyIsFullscreen(void);
int VIDDummyVdp1Reset(void);
void VIDDummyVdp1DrawStart(void);
void VIDDummyVdp1DrawEnd(void);
void VIDDummyVdp1NormalSpriteDraw(u8 * ram, Vdp1 * regs, u8* back_framebuffer);
void VIDDummyVdp1ScaledSpriteDraw(u8 * ram, Vdp1 * regs, u8* back_framebuffer);
void VIDDummyVdp1DistortedSpriteDraw(u8 * ram, Vdp1 * regs, u8* back_framebuffer);
void VIDDummyVdp1PolygonDraw(u8 * ram, Vdp1 * regs, u8* back_framebuffer);
void VIDDummyVdp1PolylineDraw(u8 * ram, Vdp1 * regs, u8* back_framebuffer);
void VIDDummyVdp1LineDraw(u8 * ram, Vdp1 * regs, u8* back_framebuffer);
void VIDDummyVdp1UserClipping(u8 * ram, Vdp1 * regs);
void VIDDummyVdp1SystemClipping(u8 * ram, Vdp1 * regs);
void VIDDummyVdp1LocalCoordinate(u8 * ram, Vdp1 * regs);
int VIDDummyVdp2Reset(void);
void VIDDummyVdp2DrawStart(void);
void VIDDummyVdp2DrawEnd(void);
void VIDDummyVdp2DrawScreens(void);
void VIDDummyGetGlSize(int *width, int *height);
void VIDDummVdp1ReadFrameBuffer(u32 type, u32 addr, void * out);
void VIDDummVdp1WriteFrameBuffer(u32 type, u32 addr, u32 val);
void VIDDummSetFilterMode(int typei,int a ){};
void VIDDummSync(){};
void VIDDummyGetNativeResolution(int *width, int * height, int *interlace);
void VIDDummyVdp2DispOff(void);
void VIDDummyOnUpdateColorRamWord(u32 addr) {};
void VIDDummyVulkanGetScreenshot(void ** outbuf, int * width, int * height) { return; }

VideoInterface_struct VIDDummy = {
	VIDCORE_DUMMY,
	"Dummy Video Interface",
	VIDDummyInit,
	VIDDummyDeInit,
	VIDDummyResize,
	VIDDummyIsFullscreen,
	VIDDummyVdp1Reset,
	VIDDummyVdp1DrawStart,
	VIDDummyVdp1DrawEnd,
	VIDDummyVdp1NormalSpriteDraw,
	VIDDummyVdp1ScaledSpriteDraw,
	VIDDummyVdp1DistortedSpriteDraw,
	VIDDummyVdp1PolygonDraw,
	VIDDummyVdp1PolylineDraw,
	VIDDummyVdp1LineDraw,
	VIDDummyVdp1UserClipping,
	VIDDummyVdp1SystemClipping,
	VIDDummyVdp1LocalCoordinate,
	VIDDummVdp1ReadFrameBuffer,
	VIDDummVdp1WriteFrameBuffer,
  VIDDummSync,
  VIDDummSync,
	VIDDummyVdp2Reset,
	VIDDummyVdp2DrawStart,
	VIDDummyVdp2DrawEnd,
	VIDDummyVdp2DrawScreens,
	VIDDummyGetGlSize,
	VIDDummSetFilterMode,
	VIDDummSync,
	VIDDummyGetNativeResolution,
	VIDDummyVdp2DispOff,
  VIDDummyOnUpdateColorRamWord,
  VIDDummyVulkanGetScreenshot
};

//////////////////////////////////////////////////////////////////////////////

int VIDDummyInit(void)
{
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyDeInit(void)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyResize(int x, int y, UNUSED unsigned int i, UNUSED unsigned int j, UNUSED int on, int aspect_rate_mode)
{
}

//////////////////////////////////////////////////////////////////////////////

int VIDDummyIsFullscreen(void)
{
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

int VIDDummyVdp1Reset(void)
{
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyVdp1DrawStart(void)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyVdp1DrawEnd(void)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyVdp1NormalSpriteDraw(u8 * ram, Vdp1 * regs, u8* back_framebuffer)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyVdp1ScaledSpriteDraw(u8 * ram, Vdp1 * regs, u8* back_framebuffer)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyVdp1DistortedSpriteDraw(u8 * ram, Vdp1 * regs, u8* back_framebuffer)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyVdp1PolygonDraw(u8 * ram, Vdp1 * regs, u8* back_framebuffer)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyVdp1PolylineDraw(u8 * ram, Vdp1 * regs, u8* back_framebuffer)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyVdp1LineDraw(u8 * ram, Vdp1 * regs, u8* back_framebuffer)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyVdp1UserClipping(u8 * ram, Vdp1 * regs)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyVdp1SystemClipping(u8 * ram, Vdp1 * regs)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyVdp1LocalCoordinate(u8 * ram, Vdp1 * regs)
{
}

//////////////////////////////////////////////////////////////////////////////

int VIDDummyVdp2Reset(void)
{
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyVdp2DrawStart(void)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyVdp2DrawEnd(void)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyVdp2DrawScreens(void)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyGetGlSize(int *width, int *height)
{
   *width = 0;
   *height = 0;
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummVdp1ReadFrameBuffer(u32 type, u32 addr, void * out)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummVdp1WriteFrameBuffer(u32 type, u32 addr, u32 val)
{
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyGetNativeResolution(int *width, int * height, int * interlace)
{
   *width = 0;
   *height = 0;
   *interlace = 0;
}

//////////////////////////////////////////////////////////////////////////////

void VIDDummyVdp2DispOff(void)
{
}

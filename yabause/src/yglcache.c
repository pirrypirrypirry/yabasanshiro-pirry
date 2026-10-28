/*  Copyright 2005-2006 Guillaume Duhamel
 *  Copyright 2005-2006 Theo Berkau
 *  Copyright 2011-2015 Shinya Miyamoto(devmiyax)

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


#include "ygl.h"
#include "yui.h"
#include "vidshared.h"
#include "vdp1.h"
#include "vdp2.h"
#include <stdlib.h>

int g_texcache_persist = 0;
/* Bumped whenever SPCTL or CCCTL change (see ygl.h); cached VDP1 textures
   from an older generation are not reused. */
u32 g_vdp1_decode_gen = 1;
int g_decodegen_on = 1;

static __thread u32 s_src_addr = 0, s_src_len = 0, s_src_addr2 = 0, s_src_len2 = 0;
static __thread u32 s_src_gen = 0;
static __thread int s_src_unit = YGL_TEXSRC_NONE;



void YglCacheSetSource(u32 srcaddr, u32 srclen, u32 srcaddr2, u32 srclen2, int unit) {
  s_src_addr = srcaddr; s_src_len = srclen;
  s_src_addr2 = srcaddr2; s_src_len2 = srclen2;
  s_src_unit = unit;
  if (unit == YGL_TEXSRC_VDP1) s_src_gen = v1_wseq;
  else if (unit == YGL_TEXSRC_VDP2) s_src_gen = v2_wseq;
  else s_src_gen = 0;
}

static int YglRangeClean(int unit, u32 srcaddr, u32 srclen, u32 gen) {
  const u32 *pages; u32 flushseq; u32 p0, p1, p;
  if (unit == YGL_TEXSRC_VDP1) { pages = v1_page_seq; flushseq = v1_flush_seq; }
  else if (unit == YGL_TEXSRC_VDP2) { pages = v2_page_seq; flushseq = v2_flush_seq; }
  else return 1;
  if ((s32)(gen - flushseq) < 0) return 0;
  p0 = (srcaddr & 0x7FFFF) >> 12;
  p1 = ((srcaddr + (srclen ? srclen - 1 : 0)) & 0x7FFFF) >> 12;
  if (p1 < p0) { p0 = 0; p1 = 127; }
  /* Acquire load pairs with the release store on the writer side (see vdp2.cpp);
     required on ARM64 to see the page stamps. */
  for (p = p0; p <= p1; p++) {
    u32 pv = __atomic_load_n(&pages[p], __ATOMIC_ACQUIRE);
    if ((s32)(pv - gen) > 0) return 0;
  }
  return 1;
}

static void YglCacheStamp(YglCacheHash * e) {
  e->srcaddr = s_src_addr; e->srclen = s_src_len;
  e->srcaddr2 = s_src_addr2; e->srclen2 = s_src_len2;
  e->gen = s_src_gen;
  e->unit = (u8)s_src_unit;
}



static u32 YglgetHash(u64 addr)
{
  /* Fold the upper key bits into the index; masking only (addr>>4) overran the table
     and left most VDP1 keys in a few buckets, since their low bits are constant per scene. */
  u64 h = (addr>>4) ^ (addr>>20) ^ (addr>>36) ^ (addr>>52);
  return (u32)(h & (HASHSIZE-1));
}

static YglCacheHash * YglgetNewCash(YglTextureManager * tm) {

  YglCacheHash * rtn;

  if (tm->CashLink_index >= HASHSIZE * 2){
    printf("not enough cash");
    return NULL;
  }
  rtn = &tm->CashLink[tm->CashLink_index];
  tm->CashLink_index++;
  return rtn;
}


static int YglIsCached_in(YglTextureManager * tm, u64 addr, YglCache * c);
int YglIsCached(YglTextureManager * tm, u64 addr, YglCache * c) {
  /* Lookup without a lock. Safe because YglCacheAdd fills a new entry completely
     before publishing the chain head with release semantics (acquire here).
     An Add may overwrite an entry with the same addr in place; that only happens
     on a VDP1/VDP2 key collision, where the unit check below rejects the hit anyway. */
  return YglIsCached_in(tm, addr, c);
}

static int YglIsCached_in(YglTextureManager * tm, u64 addr, YglCache * c) {

  u32 hashkey;

  hashkey = YglgetHash(addr);  /* get hash */

  YglCacheHash *head_val = __atomic_load_n(&tm->HashTable[hashkey], __ATOMIC_ACQUIRE);
  if (head_val == NULL) {  /* Empty Hash */
    return 0;        /* Not Found */
  }
  else {  /* needs liner search */
	  YglCacheHash *at = head_val;
    while (at != NULL) {
      if (at->addr == addr) {  /* Find! */
        /* The VDP1 and VDP2 key spaces overlap (VDP1: CMDSRCA<<48|CMDCOLR<<32|
           CMDSIZE<<16|CMDPMOD, VDP2: priority<<35|alpha<<27|paladdr<<20|charaddr),
           so a hit must come from the same unit; otherwise a background tile could
           receive a sprite texture (seen in Kingdom Grandprix). */
        if (at->unit != YGL_TEXSRC_NONE && s_src_unit != YGL_TEXSRC_NONE &&
            at->unit != s_src_unit) {
          return 0;   /* different unit: no hit */
        }
        if (g_texcache_persist) {
          if (at->unit == YGL_TEXSRC_SLOT) {
            if (at->srclen != s_src_len) return 0;
          }
          else if (!YglRangeClean(at->unit, at->srcaddr, at->srclen, at->gen) ||
                   (at->srclen2 != 0 &&
                    !YglRangeClean(at->unit, at->srcaddr2, at->srclen2, at->gen))) {
            return 0;
          }
          /* Priority/color calculation index are baked into the texels and
             depend on SPCTL/CCCTL. */
          if (g_decodegen_on && at->unit == YGL_TEXSRC_VDP1 &&
              at->decode_gen != g_vdp1_decode_gen) {
            return 0;
          }
        }
        c->x = at->x;
        c->y = at->y;
        c->no_alpha0 = at->no_alpha0; // opaque fast path: pass the flag along
        return 1;
      }
      at = at->next;
    }
    return 0;  /* Not found */
  }

  return 1;
}

//////////////////////////////////////////////////////////////////////////////

void YglCacheAdd(YglTextureManager * tm, u64 addr, YglCache * c) {

  u32 hashkey;
  YglCacheHash *add;
  hashkey = YglgetHash(addr);

  if (tm->HashTable[hashkey] == NULL){
	add = YglgetNewCash(tm);
    if (add == NULL) { YglTMRequestFlush(); return; }
    add->addr = addr;
    add->x = c->x;
    add->y = c->y;
    add->no_alpha0 = c->no_alpha0;
    add->decode_gen = g_vdp1_decode_gen; // opaque fast path
    add->next = NULL;
    YglCacheStamp(add);
	__atomic_store_n(&tm->HashTable[hashkey], add, __ATOMIC_RELEASE);
  }
  else{
	  YglCacheHash *at = tm->HashTable[hashkey];
    while (at != NULL) {
      if (at->addr == addr  ) {
        at->addr = addr;
        at->x = c->x;
        at->y = c->y;
        at->no_alpha0 = c->no_alpha0;
        at->decode_gen = g_vdp1_decode_gen; // opaque fast path
        YglCacheStamp(at);
        return;
      }
      at = at->next;
    }

	add = YglgetNewCash(tm);
    if (add == NULL) { YglTMRequestFlush(); return; }
    add->addr = addr;
    add->x = c->x;
    add->y = c->y;
    add->no_alpha0 = c->no_alpha0;
    add->decode_gen = g_vdp1_decode_gen; // opaque fast path
    YglCacheStamp(add);
	add->next = tm->HashTable[hashkey];
	__atomic_store_n(&tm->HashTable[hashkey], add, __ATOMIC_RELEASE);
  }

}

//////////////////////////////////////////////////////////////////////////////

void YglCacheReset(YglTextureManager * tm) {
	memset(tm->HashTable, 0, sizeof(tm->HashTable));
	tm->CashLink_index = 0;
}

//////////////////////////////////////////////////////////////////////////////


/*  Copyright 2015 Theo Berkau

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

#include "scsp.h"
#include "scspdsp.h"


//saturate 24 bit signed integer
static INLINE s32 saturate_24(s32 value)
{
   if (value > 8388607)
      value = 8388607;

   if (value < (-8388608))
      value = (-8388608);

   return value;
}


#define sign_x_to_s32(_bits, _value) (((int)((u32)(_value) << (32 - _bits))) >> (32 - _bits))
#define min(a,b) (a<b)?a:b

static INLINE unsigned clz(u32 v)
{
#if defined(__GNUC__) || defined(__clang__) || defined(__ICC) || defined(__INTEL_COMPILER)
  return __builtin_clz(v);
#elif defined(_MSC_VER)
  unsigned long idx;

  _BitScanReverse(&idx, v);

  return 31 ^ idx;
#else
  unsigned ret = 0;
  unsigned tmp;

  tmp = !(v & 0xFFFF0000) << 4; v <<= tmp; ret += tmp;
  tmp = !(v & 0xFF000000) << 3; v <<= tmp; ret += tmp;
  tmp = !(v & 0xF0000000) << 2; v <<= tmp; ret += tmp;
  tmp = !(v & 0xC0000000) << 1; v <<= tmp; ret += tmp;
  tmp = !(v & 0x80000000) << 0;            ret += tmp;

  return(ret);
#endif
}

//sign extended to 32 bits instead of 24
static s32 INLINE float_to_int(u16 f_val)
{
   u32 sign = (f_val >> 15) & 1;
   u32 sign_inverse = (!sign) & 1;
   u32 exponent = (f_val >> 11) & 0xf;
   u32 mantissa = f_val & 0x7FF;

   s32 ret_val = sign << 31;

   if (exponent > 11)
   {
      exponent = 11;
      ret_val |= (sign << 30);
   }
   else
      ret_val |= (sign_inverse << 30);

   ret_val |= mantissa << 19;

   ret_val = ret_val >> (exponent + (1 << 3));

   return ret_val;
}

static u16 INLINE int_to_float(u32 i_val)
{
   u32 sign = (i_val >> 23) & 1;
   u32 exponent = 0;

   if (sign != 0)
      i_val = (~i_val) & 0x7FFFFF;

   if (i_val <= 0x1FFFF)
   {
      i_val *= 64;
      exponent += 0x3000;
   }

   if (i_val <= 0xFFFFF)
   {
      i_val *= 8;
      exponent += 0x1800;
   }

   if (i_val <= 0x3FFFFF)
   {
      i_val *= 2;
      exponent += 0x800;
   }

   if (i_val <= 0x3FFFFF)
   {
      i_val *= 2;
      exponent += 0x800;
   }

   if (i_val <= 0x3FFFFF)
      exponent += 0x800;

   i_val >>= 11;
   i_val &= 0x7ff;
   i_val |= exponent;

   if (sign != 0)
      i_val ^= (0x7ff | (1 << 15));

   return i_val;
}


/* One DSP step, inline; ring_mask = (0x2000 << rbl) - 1, rbp12 = rbp << 12 (constant per sample).
   Bit-exact with the former ScspDspExec. */
/* The eleven values read and written by every step, kept local per sample (registers) instead of global. */
struct dsp_reg {
  s32 inputs; s32 y_reg; u16 frc_reg; u16 adrs_reg; u32 shift_reg; s64 product;
  u32 read_value; u32 write_value; int read_pending; int write_pending; u32 io_addr;
};
static inline __attribute__((always_inline)) void dsp_reg_load(struct dsp_reg* r, const ScspDsp* dsp)
{
  r->inputs = dsp->inputs; r->y_reg = dsp->y_reg; r->frc_reg = dsp->frc_reg; r->adrs_reg = dsp->adrs_reg;
  r->shift_reg = dsp->shift_reg; r->product = dsp->product; r->read_value = dsp->read_value;
  r->write_value = dsp->write_value; r->read_pending = dsp->read_pending; r->write_pending = dsp->write_pending;
  r->io_addr = dsp->io_addr;
}
static inline __attribute__((always_inline)) void dsp_reg_sichern(ScspDsp* dsp, const struct dsp_reg* r)
{
  dsp->inputs = r->inputs; dsp->y_reg = r->y_reg; dsp->frc_reg = r->frc_reg; dsp->adrs_reg = r->adrs_reg;
  dsp->shift_reg = r->shift_reg; dsp->product = r->product; dsp->read_value = r->read_value;
  dsp->write_value = r->write_value; dsp->read_pending = r->read_pending; dsp->write_pending = r->write_pending;
  dsp->io_addr = r->io_addr;
}

static inline __attribute__((always_inline)) void dsp_step(ScspDsp* dsp, struct dsp_reg* r, u64 instr, u16* sound_ram_16,
                                                              u32 mdec_ct, u32 ring_mask, u32 rbp12)
{
  union ScspDspInstruction inst;
  s32 ShifterOutput;
  u32 SGAOutput;
  u16 y_in;

  inst.all = instr;

  const unsigned TEMPWriteAddr = (inst.part.twa + mdec_ct) & 0x7F;
  const unsigned TEMPReadAddr = (inst.part.tra + mdec_ct) & 0x7F;
  const unsigned ira = inst.part.ira;
  const unsigned sh0 = inst.part.shift0, sh1 = inst.part.shift1;

  if (ira & 0x20) {
    if (ira & 0x10) {
      if (!(ira & 0xE))
        r->inputs = dsp->exts[ira & 0x1] << 8;
    }else{
      r->inputs = dsp->mixs[ira & 0xF] << 4;
    }
  }else{
    r->inputs = dsp->mems[ira & 0x1F];
  }

  const int INPUTS = sign_x_to_s32(24, r->inputs);
  const int TEMP = sign_x_to_s32(24, dsp->temp[TEMPReadAddr]);
  const int X = inst.part.xsel ? INPUTS : TEMP;

  /* Y and SGA read y_reg/shift_reg before they are updated further below */
  switch (inst.part.ysel) {
    case 0:  y_in = r->frc_reg; break;
    case 1:  y_in = dsp->coef[inst.part.coef]; break;
    case 2:  y_in = (u16)((r->y_reg >> 11) & 0x1FFF); break;
    default: y_in = (u16)((r->y_reg >> 4) & 0x0FFF); break;
  }
  SGAOutput = inst.part.bsel ? r->shift_reg : (u32)TEMP;

  if (inst.part.yrl) {
    r->y_reg = INPUTS & 0xFFFFFF;
  }

  ShifterOutput = (u32)sign_x_to_s32(26, r->shift_reg) << (sh0 ^ sh1);

  if (!sh1)
  {
    if(ShifterOutput > 0x7FFFFF)
      ShifterOutput = 0x7FFFFF;
    else if(ShifterOutput < -0x800000)
      ShifterOutput = 0x800000;
  }
  ShifterOutput &= 0xFFFFFF;

  if (inst.part.ewt)
    dsp->efreg[inst.part.ewa] = (ShifterOutput >> 8);

  if (inst.part.twt)
    dsp->temp[TEMPWriteAddr] = ShifterOutput;

  if (inst.part.frcl)
    r->frc_reg = (sh0 & sh1) ? (unsigned)(ShifterOutput & 0xFFF) : (unsigned)(ShifterOutput >> 11);

  r->product = ((s64)sign_x_to_s32(13, y_in) * X) >> 12;

  if (inst.part.negb)
    SGAOutput = -SGAOutput;

  if (inst.part.zero)
    SGAOutput = 0;

  r->shift_reg = (r->product + SGAOutput) & 0x3FFFFFF;

  if (inst.part.iwt)
  {
    dsp->mems[inst.part.iwa] = r->read_value;
  }

  if (r->read_pending)
  {
    u16 tmp = sound_ram_16[r->io_addr];
    r->read_value = (r->read_pending == 2) ? (tmp << 8) : float_to_int(tmp);
    r->read_pending = 0;
  }
  else if (r->write_pending)
  {
    if (!(r->io_addr & 0x40000)) {
      sound_ram_16[r->io_addr] = r->write_value;
    }
    r->write_pending = 0;
  }
  {
    u16 addr;

    addr = dsp->madrs[inst.part.masa];
    addr += inst.part.nxadr;

    if (inst.part.adreb)
    {
      addr += sign_x_to_s32(12, r->adrs_reg);
    }

    if (!inst.part.table)
    {
      addr += mdec_ct;
      addr &= ring_mask;
    }

    r->io_addr = (addr + rbp12) & 0x3FFFF;

    if (inst.part.mrd)
    {
      r->read_pending = 1 + inst.part.nofl;
    }
    if (inst.part.mwt)
    {
      r->write_pending = 1;
      r->write_value = inst.part.nofl ? (ShifterOutput >> 8) : int_to_float(ShifterOutput);
    }
    if (inst.part.adrl)
    {
      r->adrs_reg = (sh0 & sh1) ? (u16)(ShifterOutput >> 12) : (u16)((INPUTS >> 16) & 0xFFF);
    }
  }
}

void ScspDspExec(ScspDsp* dsp, int addr, u8 * sound_ram)
{
  struct dsp_reg r;
  dsp_reg_load(&r, dsp);
  dsp_step(dsp, &r, dsp->mpro[addr], (u16*)sound_ram, dsp->mdec_ct, (0x2000 << dsp->rbl) - 1, (u32)dsp->rbp << 12);
  dsp_reg_sichern(dsp, &r);
}

/* Runs of empty instructions (consecutive mpro==0): only the first two have an effect (each step completes at most one
   pending read/write); from the third on nothing is pending and the state repeats, so they are skipped.
   The step list is rebuilt whenever the program changes (g_dsp_prog_gen: both mpro write paths, load, reset). */
volatile u32 g_dsp_prog_gen = 1;
static u32 s_np_gen = 0;
static int s_np_n = -1, s_np_cnt = 0;
static u8 s_np_idx[128];

/* Runs all n steps of one sample in a single loop (no call per step). */
void ScspDspRunSample(ScspDsp* dsp, int n, u8 * sound_ram)
{
  const u32 mdec_ct = dsp->mdec_ct;
  const u32 ring_mask = (0x2000 << dsp->rbl) - 1;
  const u32 rbp12 = (u32)dsp->rbp << 12;
  u16* const ram16 = (u16*)sound_ram;
  int i, k;
  if (n > 128) n = 128;
  if (s_np_gen != g_dsp_prog_gen || s_np_n != n) {
    const u32 gen = g_dsp_prog_gen;                   /* latch before reading mpro */
    int c = 0;
    for (i = 0; i < n; i++)
      if (!(i > 1 && dsp->mpro[i] == 0 && dsp->mpro[i - 1] == 0 && dsp->mpro[i - 2] == 0))
        s_np_idx[c++] = (u8)i;
    s_np_cnt = c; s_np_n = n; s_np_gen = gen;
  }
  {
    struct dsp_reg r;   /* load once, store once */
    dsp_reg_load(&r, dsp);
    for (k = 0; k < s_np_cnt; k++)
      dsp_step(dsp, &r, dsp->mpro[s_np_idx[k]], ram16, mdec_ct, ring_mask, rbp12);
    dsp_reg_sichern(dsp, &r);
  }
}


int ScspDspAssembleGetValue(char* instruction)
{
   char temp[512] = { 0 };
   int value = 0;
   sscanf(instruction, "%s %d", temp, &value);
   return value;
}

u64 ScspDspAssembleLine(char* line)
{
   union ScspDspInstruction instruction = { 0 };

   char* temp = NULL;

   if ((temp = strstr(line, "tra")))
   {
      instruction.part.tra = ScspDspAssembleGetValue(temp);
   }

   if (strstr(line, "twt"))
   {
      instruction.part.twt = 1;
   }

   if ((temp = strstr(line, "twa")))
   {
      instruction.part.twa = ScspDspAssembleGetValue(temp);
   }

   if (strstr(line, "xsel"))
   {
      instruction.part.xsel = 1;
   }

   if ((temp = strstr(line, "ysel")))
   {
      instruction.part.ysel = ScspDspAssembleGetValue(temp);
   }

   if ((temp = strstr(line, "ira")))
   {
      instruction.part.ira = ScspDspAssembleGetValue(temp);
   }

   if (strstr(line, "iwt"))
   {
      instruction.part.iwt = 1;
   }

   if ((temp = strstr(line, "iwa")))
   {
      instruction.part.iwa = ScspDspAssembleGetValue(temp);
   }

   if (strstr(line, "table"))
   {
      instruction.part.table = 1;
   }

   if (strstr(line, "mwt"))
   {
      instruction.part.mwt = 1;
   }

   if (strstr(line, "mrd"))
   {
      instruction.part.mrd = 1;
   }

   if (strstr(line, "ewt"))
   {
      instruction.part.ewt = 1;
   }

   if ((temp = strstr(line, "ewa")))
   {
      instruction.part.ewa = ScspDspAssembleGetValue(temp);
   }

   if (strstr(line, "adrl"))
   {
      instruction.part.adrl = 1;
   }

   if (strstr(line, "frcl"))
   {
      instruction.part.frcl = 1;
   }

   if ((temp = strstr(line, "shift")))
   {
      instruction.part.shift1 = ScspDspAssembleGetValue(temp);
   }

   if (strstr(line, "yrl"))
   {
      instruction.part.yrl = 1;
   }

   if (strstr(line, "negb"))
   {
      instruction.part.negb = 1;
   }

   if (strstr(line, "zero"))
   {
      instruction.part.zero = 1;
   }

   if (strstr(line, "bsel"))
   {
      instruction.part.bsel = 1;
   }

   if (strstr(line, "nofl"))
   {
      instruction.part.nofl = 1;
   }

   if ((temp = strstr(line, "coef")))
   {
      instruction.part.coef = ScspDspAssembleGetValue(temp);
   }

   if ((temp = strstr(line, "masa")))
   {
      instruction.part.masa = ScspDspAssembleGetValue(temp);
   }

   if (strstr(line, "adreb"))
   {
      instruction.part.adreb = 1;
   }

   if (strstr(line, "nxadr"))
   {
      instruction.part.adreb = 1;
   }

   if (strstr(line, "nop"))
   {
      instruction.all = 0;
   }

   return instruction.all;
}

void ScspDspAssembleFromFile(char * filename, u64* output)
{
   int i;
   char line[1024] = { 0 };

   FILE * fp = fopen(filename, "r");

   if (!fp)
   {
      return;
   }

   for (i = 0; i < 128; i++)
   {
      char * result = fgets(line, sizeof(line), fp);
      output[i] = ScspDspAssembleLine(line);
   }
   fclose(fp);
}

void ScspDspDisasm(u8 addr, char *outstring)
{
   union ScspDspInstruction instruction;

   instruction.all = scsp_dsp.mpro[addr];

   sprintf(outstring, "%02X: ", addr);
   outstring += strlen(outstring);

   if (instruction.all == 0)
   {
      sprintf(outstring, "nop ");
      outstring += strlen(outstring);
      return;
   }

   if (instruction.part.nofl)
   {
      sprintf(outstring, "nofl ");
      outstring += strlen(outstring);
   }

   if (instruction.part.coef)
   {
      sprintf(outstring, "coef %02X ", (unsigned int)(instruction.part.coef & 0x3F));
      outstring += strlen(outstring);
   }

   if (instruction.part.masa)
   {
      sprintf(outstring, "masa %02X ", (unsigned int)(instruction.part.masa & 0x1F));
      outstring += strlen(outstring);
   }

   if (instruction.part.adreb)
   {
      sprintf(outstring, "adreb ");
      outstring += strlen(outstring);
   }

   if (instruction.part.nxadr)
   {
      sprintf(outstring, "nxadr ");
      outstring += strlen(outstring);
   }

   if (instruction.part.table)
   {
      sprintf(outstring, "table ");
      outstring += strlen(outstring);
   }

   if (instruction.part.mwt)
   {
      sprintf(outstring, "mwt ");
      outstring += strlen(outstring);
   }

   if (instruction.part.mrd)
   {
      sprintf(outstring, "mrd ");
      outstring += strlen(outstring);
   }

   if (instruction.part.ewt)
   {
      sprintf(outstring, "ewt ");
      outstring += strlen(outstring);
   }

   if (instruction.part.ewa)
   {
      sprintf(outstring, "ewa %01X ", (unsigned int)(instruction.part.ewa & 0xf));
      outstring += strlen(outstring);
   }

   if (instruction.part.adrl)
   {
      sprintf(outstring, "adrl ");
      outstring += strlen(outstring);
   }

   if (instruction.part.frcl)
   {
      sprintf(outstring, "frcl ");
      outstring += strlen(outstring);
   }

   if (instruction.part.shift1)
   {
      sprintf(outstring, "shift %d ", (int)(instruction.part.shift1 & 3));
      outstring += strlen(outstring);
   }

   if (instruction.part.yrl)
   {
      sprintf(outstring, "yrl ");
      outstring += strlen(outstring);
   }

   if (instruction.part.negb)
   {
      sprintf(outstring, "negb ");
      outstring += strlen(outstring);
   }

   if (instruction.part.zero)
   {
      sprintf(outstring, "zero ");
      outstring += strlen(outstring);
   }

   if (instruction.part.bsel)
   {
      sprintf(outstring, "bsel ");
      outstring += strlen(outstring);
   }

   if (instruction.part.xsel)
   {
      sprintf(outstring, "xsel ");
      outstring += strlen(outstring);
   }

   if (instruction.part.ysel)
   {
      sprintf(outstring, "ysel %d ", (int)(instruction.part.ysel & 3));
      outstring += strlen(outstring);
   }

   if (instruction.part.ira)
   {
      sprintf(outstring, "ira %02X ", (int)(instruction.part.ira & 0x3F));
      outstring += strlen(outstring);
   }

   if (instruction.part.iwt)
   {
      sprintf(outstring, "iwt ");
      outstring += strlen(outstring);
   }

   if (instruction.part.iwa)
   {
      sprintf(outstring, "iwa %02X ", (unsigned int)(instruction.part.iwa & 0x1F));
      outstring += strlen(outstring);
   }

   if (instruction.part.tra)
   {
      sprintf(outstring, "tra %02X ", (unsigned int)(instruction.part.tra & 0x7F));
      outstring += strlen(outstring);
   }

   if (instruction.part.twt)
   {
      sprintf(outstring, "twt ");
      outstring += strlen(outstring);
   }

   if (instruction.part.twa)
   {
      sprintf(outstring, "twa %02X ", (unsigned int)(instruction.part.twa & 0x7F));
      outstring += strlen(outstring);
   }

   if (instruction.part.unknown)
   {
      sprintf(outstring, "unknown ");
      outstring += strlen(outstring);
   }

   if (instruction.part.unknown2)
   {
      sprintf(outstring, "unknown2 ");
      outstring += strlen(outstring);
   }

//   if (instruction.part.unknown3)
 //  {
 //     sprintf(outstring, "unknown3 %d", (int)(instruction.part.unknown3 & 3));
 //     outstring += strlen(outstring);
 //  }
}

void ScspDspDisassembleToFile(char * filename)
{
   int i;
   FILE * fp = fopen(filename, "w");

   if (!fp)
   {
      return;
   }

   for (i = 0; i < 128; i++)
   {
      char output[1024] = { 0 };
      ScspDspDisasm(i, output);
      fprintf(fp, "%s\n", output);
   }

   fclose(fp);
}

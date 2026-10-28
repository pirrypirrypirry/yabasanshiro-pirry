/* Fast path 2 of the SCU DSP interpreter, hand-specialised from the generic path in scu.c (keep both in sync).
   Stage A: PC, CT, RX, RY, AC, P, ALU, ProgControlPort and jmpaddr live in locals for the whole loop as an exact
     mirror of the memory state: everything is written back before any external call (DV_SYNC) and reloaded
     afterwards (DV_LOAD). TOP, LOP, delayed, RA0, WA0, dsp_dma_wait, MD and ProgramRam stay in memory. CT is packed
     into one u32 (lane k = CT[k]).
   Fast/slow iterations: an iteration runs fast only if T0 == 0 and dsp_dma_wait <= 0 at its head. Then nothing in the
     iteration calls step_dsp_dma (only the DMA instruction writes dsp_dma_wait/T0, at its end), so no wait checks are
     needed per MD access. Otherwise dv_langsam() executes exactly one iteration with the stage A code (step_dsp_dma at
     the same places as ScuDspExecInner); DMA03/07 with sel=4 and ports 0x80/0x84 may change any field there, so all
     state is reloaded afterwards.
   Stage B: predecoding into s_vor[256], 16 bytes each (two u64), tagged with the raw instruction word. If
     raw != ProgramRam[pc] the entry is decoded again, so every writer of program RAM (init/reset, state load, port
     0x84, DMA sel=4 during a run, debugger) is covered without explicit invalidation. Zero-initialised = decode(0).
     The cache deliberately lives outside scudspregs_struct. incFlg is static per instruction and precomputed.
     Dispatch uses computed goto over offset tables (no range check, no relocations).
   Semantics follow ScuDspExecInner (MOV ALU,A uses the ALU in memory; ScuDspExecReg uses a local copy and differs
   only when a step overwrites the ALU). */

/* ---- slow path: exactly one iteration, stage A (macros DA_) ----
   Entered only if T0 != 0 or dsp_dma_wait > 0 at the iteration head. step_dsp_dma runs at the same places as in
   ScuDspExecInner (head, readgensrc, writed1busdest, writeloadimdest, DMA, END). */
#ifdef WORDS_BIGENDIAN
#define DA_SH(k) ((3u - (u32)(k)) * 8u)
#else
#define DA_SH(k) ((u32)(k) * 8u)
#endif
#define DA_LANE(k)    (0xFFu << DA_SH(k))
#define DA_CT(k)      ((ctp >> DA_SH(k)) & 0x3Fu)
#define DA_CTSET(k,v) (ctp = (ctp & ~DA_LANE(k)) | ((u32)(u8)(v) << DA_SH(k)))
#define DA_CTINC1(k)  (ctp = (ctp & ~DA_LANE(k)) | ((((ctp >> DA_SH(k)) + 1u) & 0x3Fu) << DA_SH(k)))
/* all lanes in m: CT = (CT+1) & 0x3F; (CT&0x3F)+1 <= 0x40, so no carry into the neighbouring lane */
#define DA_CTINC(m)   (ctp = (ctp & ~(m)) | (((ctp & 0x3F3F3F3Fu) + ((m) & 0x01010101u)) & (m) & 0x3F3F3F3Fu))

#define DA_SYNC() do { DL->PC = (u8)pc; DL->TOP = (u8)top; DL->LOP = (u16)lop; DL->jmpaddr = jmp; DL->delayed = dly; \
      __builtin_memcpy(DL->CT, &ctp, 4); DL->RX = rx; DL->RY = ry; DL->AC.all = ac.all; DL->P.all = p.all; \
      DL->ALU.all = alu.all; DL->ProgControlPort.all = pcp.all; DL->dsp_dma_wait = dw; } while (0)
#define DA_LOAD() do { pc = DL->PC; top = DL->TOP; lop = DL->LOP; jmp = DL->jmpaddr; dly = DL->delayed; \
      __builtin_memcpy(&ctp, DL->CT, 4); rx = DL->RX; ry = DL->RY; ac.all = DL->AC.all; p.all = DL->P.all; \
      alu.all = DL->ALU.all; pcp.all = DL->ProgControlPort.all; dw = DL->dsp_dma_wait; } while (0)
#define DA_STEP() do { DA_SYNC(); step_dsp_dma(DL); DA_LOAD(); } while (0)
/* "Finish Previous DMA operation" */
#define DA_WAIT() do { if (__builtin_expect(dw > 0, 0)) { dw = 0; DA_STEP(); } } while (0)

/* readgensrc */
#define DA_RD(numexpr) ({ u32 _n = (numexpr), _v; \
      if (_n <= 7) { inc |= ((_n >> 2) & 1u) ? DA_LANE(_n & 3u) : 0u; DA_WAIT(); _v = DL->MD[_n & 3u][DA_CT(_n & 3u)]; } \
      else if (_n == 0x9) _v = (u32)alu.part.L; \
      else if (_n == 0xA) _v = (u32)(alu.all >> 16); \
      else _v = 0xFFFFFFFFu; \
      _v; })

/* writed1busdest */
#define DA_WD1(numexpr, valexpr) do { u32 _n = (numexpr), _v = (valexpr); DA_WAIT(); \
      switch (_n) { \
         case 0x0: case 0x1: case 0x2: case 0x3: DL->MD[_n][DA_CT(_n)] = _v; inc |= DA_LANE(_n); break; \
         case 0x4: rx = _v; break; \
         case 0x5: p.all = (signed)_v; break; \
         case 0x6: DL->RA0 = _v; break; \
         case 0x7: DL->WA0 = _v; break; \
         case 0xA: lop = (u16)_v; break; \
         case 0xB: top = (u8)_v; break; \
         case 0xC: case 0xD: case 0xE: case 0xF: DA_CTSET(_n & 3u, _v); inc &= ~DA_LANE(_n & 3u); break; \
         default: break; } } while (0)

/* writeloadimdest */
#define DA_WLI(numexpr, valexpr) do { u32 _n = (numexpr), _v = (valexpr); DA_WAIT(); \
      switch (_n) { \
         case 0x0: case 0x1: case 0x2: case 0x3: DL->MD[_n][DA_CT(_n)] = _v; inc |= DA_LANE(_n); break; \
         case 0x4: rx = _v; break; \
         case 0x5: p.all = (s32)_v; break; \
         case 0x6: DL->RA0 = (_v & 0x1FFFFFF); break; \
         case 0x7: DL->WA0 = (_v & 0x1FFFFFF); break; \
         case 0xA: lop = (u16)(_v & 0x0FFF); break; \
         case 0xC: top = (u8)(pc + 1u); jmp = (s32)_v; dly = 0; break; \
         default: break; } } while (0)

static s32 __attribute__((noinline, cold)) dv_langsam(scudspregs_struct *const DL, s32 dsp_counter) {
   u32 pc, top, lop, ctp, inc; s32 jmp, dw, rx, ry; int dly;
   __typeof__(DL->AC) ac; __typeof__(DL->P) p; __typeof__(DL->ALU) alu;
   __typeof__(DL->ProgControlPort) pcp;
   DA_LOAD();
   do {
      u32 instruction;
      if (pcp.part.T0 != 0) DA_STEP();
      instruction = DL->ProgramRam[pc];
      inc = 0;
      alu.all = ac.all;

      switch (instruction >> 26) {
         case 0x0: break;
         case 0x1: // AND
            alu.part.L = (s64)((u32)ac.part.L & (u32)p.part.L);
            pcp.part.Z = (alu.part.L == 0); pcp.part.S = ((s64)alu.part.L < 0); pcp.part.C = 0;
            break;
         case 0x2: // OR
            alu.part.L = (u64)((u32)ac.part.L | (u32)p.part.L);
            pcp.part.Z = (alu.part.L == 0); pcp.part.S = ((s64)alu.part.L < 0); pcp.part.C = 0;
            break;
         case 0x3: // XOR
            alu.part.L = (u64)((u32)ac.part.L ^ (u32)p.part.L);
            pcp.part.Z = (alu.part.L == 0); pcp.part.S = ((s64)alu.part.L < 0); pcp.part.C = 0;
            break;
         case 0x4: // ADD
            alu.part.L = (s32)ac.part.L + (s32)p.part.L;
            pcp.part.Z = (alu.part.L == 0); pcp.part.S = ((s32)alu.part.L < 0);
            pcp.part.C = ((((u64)(u32)p.part.L + (u64)(u32)ac.part.L) & 0x100000000) != 0);
            break;
         case 0x5: // SUB
            alu.part.L = (s32)ac.part.L - (s32)p.part.L;
            pcp.part.Z = (alu.part.L == 0); pcp.part.S = ((s64)alu.part.L < 0);
            pcp.part.C = (((((u64)(u32)ac.part.L - (u64)(u32)p.part.L)) & 0x100000000) != 0);
            break;
         case 0x6: // AD2
            alu.all = (s64)ac.all + (s64)p.all;
            pcp.part.Z = (alu.all == 0); pcp.part.S = ((alu.all & 0x800000000000) != 0);
            pcp.part.C = ((((ac.all & 0xffffffffffff) + (p.all & 0xffffffffffff)) & (0x1000000000000)) != 0);
            break;
         case 0x8: // SR
            pcp.part.C = ac.part.L & 0x1;
            alu.part.L = (ac.part.L & 0x80000000) | (ac.part.L >> 1);
            pcp.part.Z = (alu.part.L == 0); pcp.part.S = ((alu.part.L & 0x80000000) != 0);
            break;
         case 0x9: // RR
            pcp.part.C = ac.part.L & 0x1;
            alu.part.L = ((u32)(pcp.part.C) << 31) | ((u32)(ac.part.L) >> 1);
            pcp.part.Z = (alu.part.L == 0); pcp.part.S = ((alu.part.L & 0x80000000) != 0);
            break;
         case 0xA: // SL
            pcp.part.C = (ac.part.L >> 31) & 0x01;
            alu.part.L = (u32)(ac.part.L << 1);
            pcp.part.Z = (alu.part.L == 0); pcp.part.S = ((alu.part.L & 0x80000000) != 0);
            break;
         case 0xB: // RL
            pcp.part.C = (ac.part.L >> 31) & 0x01;
            alu.part.L = (((u32)ac.part.L << 1) | pcp.part.C);
            pcp.part.Z = (alu.part.L == 0); pcp.part.S = ((alu.part.L & 0x80000000) != 0);
            break;
         case 0xF: // RL8
            pcp.part.C = (ac.part.L >> 24) & 0x01;
            alu.part.L = ((u32)(ac.part.L << 8) | ((ac.part.L >> 24) & 0xFF));
            pcp.part.Z = (alu.part.L == 0); pcp.part.S = ((alu.part.L & 0x80000000) != 0);
            break;
         default: break;
      }

      switch (instruction >> 30) {
      case 0x00: // Operation Commands
         switch ((instruction >> 23) & 0x3) {
            case 2: p.all = (s64)rx * (s32)ry; break;
            case 3: p.all = (s64)(s32)DA_RD((instruction >> 20) & 0x7); break;
            default: break;
         }
         if ((instruction >> 23) & 0x4) rx = DA_RD((instruction >> 20) & 0x7);
         if ((instruction >> 17) & 0x4) ry = DA_RD((instruction >> 14) & 0x7);
         switch ((instruction >> 17) & 0x3) {
            case 1: ac.all = 0; break;
            case 2: ac.all = alu.all; break;
            case 3: ac.all = (s64)(s32)DA_RD((instruction >> 14) & 0x7); break;
            default: break;
         }
         switch ((instruction >> 12) & 0x3) {
            case 1: // MOV SImm,[d]
               if (inc) { DA_CTINC(inc); inc = 0; }
               DA_WD1((instruction >> 8) & 0xF, (u32)(signed char)(instruction & 0xFF));
               break;
            case 3: { // MOV [s],[d]
               u32 sv = DA_RD(instruction & 0xF);
               DA_WD1((instruction >> 8) & 0xF, sv);
               break; }
            default: break;
         }
         break;
      case 0x02: // Load Immediate Commands
         if ((instruction >> 25) & 1) {
            const u32 v = (instruction & 0x7FFFF) | ((instruction & 0x40000) ? 0xFFF80000 : 0x00000000);
            int ok;
            switch ((instruction >> 19) & 0x3F) {
               case 0x01: ok = !pcp.part.Z; break;
               case 0x02: ok = !pcp.part.S; break;
               case 0x03: ok = (pcp.part.Z == 0 && pcp.part.S == 0); break;
               case 0x04: ok = !pcp.part.C; break;
               case 0x08: ok = !pcp.part.T0; break;
               case 0x21: ok = pcp.part.Z; break;
               case 0x22: ok = pcp.part.S; break;
               case 0x23: ok = (pcp.part.Z || pcp.part.S); break;
               case 0x24: ok = pcp.part.C; break;
               case 0x28: ok = pcp.part.T0; break;
               default: ok = 0; break;
            }
            if (ok) DA_WLI((instruction >> 26) & 0xF, v);
         } else {
            int value = (instruction & 0x1FFFFFF);
            if (value & 0x1000000) value |= 0xfe000000;
            DA_WLI((instruction >> 26) & 0xF, (u32)value);
         }
         break;
      case 0x03: // Other
         switch ((instruction >> 28) & 0xF) {
            case 0x0C: { // DMA
               DA_WAIT();
               DL->dsp_dma_instruction = instruction;
               pcp.part.T0 = 1;
               int Counter = 0;
               if ( ((instruction >> 10) & 0x1F) == 0x00 || ((instruction >> 10) & 0x1F) == 0x04 ||
                    ((instruction >> 11) & 0x0F) == 0x08 || ((instruction >> 10) & 0x1F) == 0x14 ) {
                  Counter = instruction & 0xFF;
               } else if ( ((instruction >> 11) & 0x0F) == 0x04 || ((instruction >> 10) & 0x1F) == 0x0C ||
                           ((instruction >> 11) & 0x0F) == 0x0C || ((instruction >> 10) & 0x1F) == 0x1C ) {
                  const u32 k = instruction & 0x3;
                  Counter = DL->MD[k][DA_CT(k)];
                  if (instruction & 0x4) DA_CTINC1(k);
               }
               DL->dsp_dma_size = Counter;
               dw = Counter >> 12;
               DL->WA0M = DL->WA0;
               DL->RA0M = DL->RA0;
               break; }
            case 0x0D: // JMP
               if (jmp != (s32)0xffffffff) break;
               {
                  int ok;
                  switch ((instruction >> 19) & 0x7F) {
                     case 0x00: ok = 1; break;
                     case 0x41: ok = !pcp.part.Z; break;
                     case 0x42: ok = !pcp.part.S; break;
                     case 0x43: ok = (pcp.part.Z == 0 && pcp.part.S == 0); break;
                     case 0x44: ok = !pcp.part.C; break;
                     case 0x48: ok = !pcp.part.T0; break;
                     case 0x61: ok = pcp.part.Z; break;
                     case 0x62: ok = pcp.part.S; break;
                     case 0x63: ok = (pcp.part.Z || pcp.part.S); break;
                     case 0x64: ok = pcp.part.C; break;
                     case 0x68: ok = pcp.part.T0; break;
                     default: ok = 0; break;
                  }
                  if (ok) { jmp = instruction & 0xFF; dly = 0; }
               }
               break;
            case 0x0E: // LPS / BTM
               if (lop != 0) { jmp = (instruction & 0x8000000) ? (s32)pc : (s32)top; dly = 0; lop--; }
               else if (!(instruction & 0x8000000)) lop = (lop - 1) & 0xFFF;   /* BTM always decrements LOP (same as ScuDspExecInner) */
               break;
            case 0x0F: // END
               {  /* finish an open DMA at END, as in the fast path (and Kronos, Yabause 1.20.37) */
                  if (dw > 0) {
                     dw = 0;
                     DA_STEP();
                  }
               }
               pcp.part.EX = 0;
               if (instruction & 0x8000000) {
                  pcp.part.E = 1;
                  DA_SYNC(); ScuSendDSPEnd(); DA_LOAD();
               }
               pcp.part.P = pc + 1;
               dsp_counter = 1;
               break;
            default: break;
         }
         break;
      default: break;
      }

      if (inc) DA_CTINC(inc);
      pc = (pc + 1u) & 0xFFu;
      if (jmp != (s32)0xFFFFFFFF) {
         if (dly) { pc = (u8)jmp; jmp = (s32)0xFFFFFFFF; dsp_counter += 1; }
         else dly = 1;
      }
      dsp_counter--;
   } while (0);
   DA_SYNC();
   return dsp_counter;
}


#ifdef WORDS_BIGENDIAN
#define DV_SH(k)      ((3u - (u32)(k)) * 8u)
#define DV_ROWW(sh)   ((24u - (sh)) << 3)            /* word index of the MD row for lane shift sh */
#else
#define DV_SH(k)      ((u32)(k) * 8u)
#define DV_ROWW(sh)   ((sh) << 3)
#endif
#define DV_LANE(k)    (0xFFu << DV_SH(k))
#define DV_CT(k)      ((ctp >> DV_SH(k)) & 0x3Fu)
#define DV_CTSET(k,v) (ctp = (ctp & ~DV_LANE(k)) | ((u32)(u8)(v) << DV_SH(k)))
#define DV_CTINC1(k)  (ctp = (ctp & ~DV_LANE(k)) | ((((ctp >> DV_SH(k)) + 1u) & 0x3Fu) << DV_SH(k)))
/* all lanes in m: CT = (CT+1) & 0x3F; (CT&0x3F)+1 <= 0x40, so no carry into the neighbouring lane */
#define DV_CTINC(m)   (ctp = (ctp & ~(m)) | (((ctp & 0x3F3F3F3Fu) + ((m) & 0x01010101u)) & (m) & 0x3F3F3F3Fu))
/* MD element of the bank with lane shift sh at the current CT position */
#define DV_MD(sh)     (mdb[DV_ROWW(sh) + ((ctp >> (sh)) & 0x3Fu)])
/* Z/S/C in a single write (conditions identical to the generic path) */
#define DV_FLAGS(z,s,c) (pcp.all = (pcp.all & ~(fz | fs | fc)) | ((z) ? fz : 0u) | ((s) ? fs : 0u) | ((c) ? fc : 0u))

#define DV_SYNC() do { DL->PC = (u8)pc; DL->jmpaddr = jmp; __builtin_memcpy(DL->CT, &ctp, 4); \
      DL->RX = rx; DL->RY = ry; DL->AC.all = ac.all; DL->P.all = p.all; DL->ALU.all = alu.all; \
      DL->ProgControlPort.all = pcp.all; } while (0)
#define DV_LOAD() do { pc = DL->PC; jmp = DL->jmpaddr; __builtin_memcpy(&ctp, DL->CT, 4); \
      rx = DL->RX; ry = DL->RY; ac.all = DL->AC.all; p.all = DL->P.all; alu.all = DL->ALU.all; \
      pcp.all = DL->ProgControlPort.all; } while (0)

/* ---- predecoding: entry = two u64 ----
   a: bits 0-31 raw word (tag) | bits 32-63 imm  (class 0: sign-extended SImm, or source lane shift (MD) / source
                                                  number 8-F | MVI: value | JMP: target)
   b: bits 0-31  m    (class 0: incFlg lanes at instruction end, for D1 SImm those applied before the write ·
                       MVI/JMP: flag mask)
      bits 32-36 kind (0 pure NOP without bus work | 1-15 class 0 with ALU op, 7 = ALU NOP with bus work | from 16 DV_K_*)
      bit 40 P=MUL | bit 41 P=[s1] | bit 42 X=[s1] | bit 43 Y=[s2]  (MVI/JMP: bit 40 = polarity,
                                                                      execute if ((pcp & m) != 0) == bit 40)
      bits 44-45 A op (1 CLR, 2 ALU, 3 [s2]) | bits 46-48 D1 code (DV_D1_*)
      bits 49-53 lane shift s1 | bits 54-58 lane shift s2
      bits 59-63 D1 destination: MD = lane shift, else destination number 4-15 | MVI: destination number 0-15 */
typedef struct { u64 a, b; } dv_ent;
enum { DV_K_MVI = 16, DV_K_DMA, DV_K_JMP, DV_K_LPS, DV_K_BTM, DV_K_END, DV_K_NIX };
enum { DV_D1_NIX = 0, DV_D1_IMM_MD, DV_D1_IMM_REG, DV_D1_MD_MD, DV_D1_MD_REG, DV_D1_SP_MD, DV_D1_SP_REG };
static dv_ent s_vor[256];                       /* zero-initialised = decode(0) */
static int s_vorh[256], s_vord[256];   /* label offsets per slot; 0/0 = k_nop/d_nix = effect of decode(0) */

static inline u32 dv_flagmaske(int z, int s, int c, int t0) {
   __typeof__(ScuDsp->ProgControlPort) u; u.all = 0;
   u.part.Z = z; u.part.S = s; u.part.C = c; u.part.T0 = t0;
   return u.all;
}
/* Condition (low 6 bits of MVI/JMP): 01 Z, 02 S, 03 Z|S, 04 C, 08 T0; bit 0x20 = positive polarity; otherwise never */
static void dv_bedingung(u32 *m, u32 *pol, u32 code) {
   const u32 b = code & 0x1F;
   if (b == 1 || b == 2 || b == 3 || b == 4 || b == 8) {
      *m = dv_flagmaske(b == 1 || b == 3, b == 2 || b == 3, b == 4, b == 8);
      *pol = (code & 0x20) ? 1 : 0;
   } else { *m = 0; *pol = 1; }                  /* never */
}
static void __attribute__((noinline, cold)) dv_dekodiere(dv_ent *goal, u32 raw) {
   u32 imm = 0, m = 0, kind = 0, ops = 0, aop = 0, d1c = 0, sh1 = 0, sh2 = 0, dfeld = 0;
   switch (raw >> 30) {
   case 0: {
      u32 alu = (raw >> 26) & 0xF;
      if (alu == 0x7 || alu == 0xC || alu == 0xD || alu == 0xE) alu = 0;
      kind = alu;
      const u32 pop = (raw >> 23) & 3, x = (raw >> 25) & 1, y = (raw >> 19) & 1;
      const u32 s1 = (raw >> 20) & 7, s2 = (raw >> 14) & 7, d1 = (raw >> 12) & 3, dst = (raw >> 8) & 0xF;
      aop = (raw >> 17) & 3;
      if ((pop == 3 || x) && (s1 & 4)) m |= DV_LANE(s1 & 3);
      if ((y || aop == 3) && (s2 & 4)) m |= DV_LANE(s2 & 3);
      ops = (pop == 2 ? 1u : 0u) | (pop == 3 ? 2u : 0u) | (x << 2) | (y << 3);
      if (d1 == 1 || d1 == 3) {
         const int zielmd = dst <= 3;
         dfeld = zielmd ? DV_SH(dst) : dst;
         if (d1 == 1) { d1c = zielmd ? DV_D1_IMM_MD : DV_D1_IMM_REG; imm = (u32)(signed char)(raw & 0xFF); }
         else {
            const u32 src = raw & 0xF;
            if (src <= 7) { d1c = zielmd ? DV_D1_MD_MD : DV_D1_MD_REG; imm = DV_SH(src & 3); if (src & 4) m |= DV_LANE(src & 3); }
            else { d1c = zielmd ? DV_D1_SP_MD : DV_D1_SP_REG; imm = src; }
            if (zielmd) m |= DV_LANE(dst);
            else if (dst >= 0xC) m &= ~DV_LANE(dst & 3);
         }
      }
      sh1 = DV_SH(s1 & 3); sh2 = DV_SH(s2 & 3);
      /* ALU NOP: kind 0 = no work at all (straight to the end, so decode(0) stays all zero), kind 7 = bus work */
      if (kind == 0 && (ops | aop | d1c) != 0) kind = 7;
      break; }
   case 2:
      kind = DV_K_MVI; dfeld = (raw >> 26) & 0xF;
      if ((raw >> 25) & 1) {
         imm = (raw & 0x7FFFF) | ((raw & 0x40000) ? 0xFFF80000 : 0x00000000);
         dv_bedingung(&m, &ops, (raw >> 19) & 0x3F);
      } else {
         int value = (raw & 0x1FFFFFF);
         if (value & 0x1000000) value |= 0xfe000000;
         imm = (u32)value; m = 0; ops = 0;        /* always */
      }
      break;
   case 3:
      switch ((raw >> 28) & 0xF) {
         case 0xC: kind = DV_K_DMA; break;
         case 0xD: {
            const u32 code = (raw >> 19) & 0x7F;
            kind = DV_K_JMP; imm = raw & 0xFF;
            if (code == 0) { m = 0; ops = 0; }
            else if (code & 0x40) dv_bedingung(&m, &ops, code & 0x3F);
            else { m = 0; ops = 1; }
            break; }
         case 0xE: kind = (raw & 0x8000000) ? DV_K_LPS : DV_K_BTM; break;
         default:  kind = DV_K_END; break;
      }
      break;
   default: kind = DV_K_NIX; break;             /* class 1: invalid, does nothing */
   }
   goal->a = (u64)raw | ((u64)imm << 32);
   goal->b = (u64)m | ((u64)kind << 32) | ((u64)ops << 40) | ((u64)aop << 44) | ((u64)d1c << 46)
           | ((u64)sh1 << 49) | ((u64)sh2 << 54) | ((u64)dfeld << 59);
}

static void __attribute__((noinline)) ScuDspExecPrecompiled(u32 timing) {
   scudspregs_struct *const DL = ScuDsp;
   u32 *const mdb = &DL->MD[0][0];
   u32 pc, ctp; s32 jmp, rx, ry;
   __typeof__(DL->AC) ac; __typeof__(DL->P) p; __typeof__(DL->ALU) alu;
   __typeof__(DL->ProgControlPort) pcp;
   const u32 t0m = dv_flagmaske(0, 0, 0, 1), fz = dv_flagmaske(1, 0, 0, 0), fs = dv_flagmaske(0, 1, 0, 0),
             fc = dv_flagmaske(0, 0, 1, 0);
   /* jump tables as offsets (no relocations), index = kind or D1 code */
   static const int k_tab[32] = {
      &&ende0 - &&k_nop, &&k_and - &&k_nop, &&k_or - &&k_nop, &&k_xor - &&k_nop, &&k_add - &&k_nop, &&k_sub - &&k_nop,
      &&k_ad2 - &&k_nop, &&k_nop - &&k_nop, &&k_sr - &&k_nop, &&k_rr - &&k_nop, &&k_sl - &&k_nop, &&k_rl - &&k_nop,
      &&k_nop - &&k_nop, &&k_nop - &&k_nop, &&k_nop - &&k_nop, &&k_rl8 - &&k_nop,
      &&k_mvi - &&k_nop, &&k_dma - &&k_nop, &&k_jmp - &&k_nop, &&k_lps - &&k_nop, &&k_btm - &&k_nop, &&k_end - &&k_nop,
      &&ende0 - &&k_nop, &&ende0 - &&k_nop, &&ende0 - &&k_nop, &&ende0 - &&k_nop, &&ende0 - &&k_nop, &&ende0 - &&k_nop,
      &&ende0 - &&k_nop, &&ende0 - &&k_nop, &&ende0 - &&k_nop, &&ende0 - &&k_nop };
   static const int d_tab[8] = {
      &&d_nix - &&d_nix, &&d_imm_md - &&d_nix, &&d_imm_reg - &&d_nix, &&d_md_md - &&d_nix, &&d_md_reg - &&d_nix,
      &&d_sp_md - &&d_nix, &&d_sp_reg - &&d_nix, &&d_nix - &&d_nix };
   s32 dsp_counter = (s32)timing;
   int pruef = 1;   /* check T0/dsp_dma_wait only where they can change */
   u32 inc = 0, v = 0;
   u64 ea = 0, eb = 0;
/* fetch the next instruction and jump straight into its handler (replicated at every instruction end) */
#define DV_NEXT() do { ea = s_vor[pc].a; eb = s_vor[pc].b; \
      if (__builtin_expect((u32)ea != DL->ProgramRam[pc], 0)) goto new_val; \
      alu.all = ac.all; goto *(&&k_nop + s_vorh[pc]); } while (0)
#define DV_TAIL0() do { pc = (pc + 1u) & 0xFFu;                   /* u8 wrap-around */ \
      if (__builtin_expect(jmp != (s32)0xFFFFFFFF, 0)) {            /* MVI -1 to PC leaves jmpaddr = 0xFFFFFFFF */ \
         if (DL->delayed) { pc = (u8)jmp; jmp = (s32)0xFFFFFFFF; dsp_counter += 1; } \
         else DL->delayed = 1; } \
      if (--dsp_counter <= 0) goto aus; \
      if (__builtin_expect(pruef, 0)) goto head_val; \
      DV_NEXT(); } while (0)
#define DV_TAIL_INC() do { if (inc) DV_CTINC(inc); DV_TAIL0(); } while (0)
   DV_LOAD();
   if (dsp_counter <= 0) goto aus;
   head_val:
      if (__builtin_expect((pcp.all & t0m) != 0 || DL->dsp_dma_wait > 0, 0)) {   /* DMA pending: run one exact stage A iteration */
         DV_SYNC();
         dsp_counter = dv_langsam(DL, dsp_counter);
         DV_LOAD();
         if (dsp_counter <= 0) goto aus;
         goto head_val;
      }
      pruef = 0;
      DV_NEXT();
   new_val: {   /* cold: SYNC/LOAD bracket, re-decode and fill the label offsets */
         dv_ent *const e = &s_vor[pc];
         DV_SYNC(); dv_dekodiere(e, DL->ProgramRam[pc]); DV_LOAD();
         ea = e->a; eb = e->b;
         s_vorh[pc] = k_tab[(u32)(eb >> 32) & 0x1Fu];
         s_vord[pc] = d_tab[(u32)(eb >> 46) & 7u];
         alu.all = ac.all;
         goto *(&&k_nop + s_vorh[pc]);
      }

      /* ---------- class 0: ALU ---------- */
   k_and:
      alu.part.L = (s64)((u32)ac.part.L & (u32)p.part.L);
      DV_FLAGS(alu.part.L == 0, (s64)alu.part.L < 0, 0);
      goto k0;
   k_or:
      alu.part.L = (u64)((u32)ac.part.L | (u32)p.part.L);
      DV_FLAGS(alu.part.L == 0, (s64)alu.part.L < 0, 0);
      goto k0;
   k_xor:
      alu.part.L = (u64)((u32)ac.part.L ^ (u32)p.part.L);
      DV_FLAGS(alu.part.L == 0, (s64)alu.part.L < 0, 0);
      goto k0;
   k_add:
      alu.part.L = (s32)ac.part.L + (s32)p.part.L;
      DV_FLAGS(alu.part.L == 0, (s32)alu.part.L < 0, ((u64)(u32)p.part.L + (u64)(u32)ac.part.L) & 0x100000000);
      goto k0;
   k_sub:
      alu.part.L = (s32)ac.part.L - (s32)p.part.L;
      DV_FLAGS(alu.part.L == 0, (s64)alu.part.L < 0, (((u64)(u32)ac.part.L - (u64)(u32)p.part.L)) & 0x100000000);
      goto k0;
   k_ad2:
      alu.all = (s64)ac.all + (s64)p.all;
      DV_FLAGS(alu.all == 0, alu.all & 0x800000000000,
               ((ac.all & 0xffffffffffff) + (p.all & 0xffffffffffff)) & (0x1000000000000));
      goto k0;
   k_sr: {
      const u32 c = ac.part.L & 0x1;
      alu.part.L = (ac.part.L & 0x80000000) | (ac.part.L >> 1);
      DV_FLAGS(alu.part.L == 0, alu.part.L & 0x80000000, c);
      goto k0; }
   k_rr: {
      const u32 c = ac.part.L & 0x1;
      alu.part.L = ((u32)(c) << 31) | ((u32)(ac.part.L) >> 1);
      DV_FLAGS(alu.part.L == 0, alu.part.L & 0x80000000, c);
      goto k0; }
   k_sl: {
      const u32 c = (ac.part.L >> 31) & 0x01;
      alu.part.L = (u32)(ac.part.L << 1);
      DV_FLAGS(alu.part.L == 0, alu.part.L & 0x80000000, c);
      goto k0; }
   k_rl: {
      const u32 c = (ac.part.L >> 31) & 0x01;
      alu.part.L = (((u32)ac.part.L << 1) | c);
      DV_FLAGS(alu.part.L == 0, alu.part.L & 0x80000000, c);
      goto k0; }
   k_rl8: {
      const u32 c = (ac.part.L >> 24) & 0x01;
      alu.part.L = ((u32)(ac.part.L << 8) | ((ac.part.L >> 24) & 0xFF));
      DV_FLAGS(alu.part.L == 0, alu.part.L & 0x80000000, c);
      goto k0; }

   k_nop:
   k0: {  /* ---------- class 0: buses (same order as ScuDspExecInner; dw <= 0, so no step) ---------- */
         const u32 sh1 = (u32)(eb >> 49) & 0x1F, sh2 = (u32)(eb >> 54) & 0x1F;
         if (eb & (1ull << 40)) p.all = (s64)rx * (s32)ry;             // MOV MUL,P
         if (eb & (1ull << 41)) p.all = (s64)(s32)DV_MD(sh1);           // MOV [s],P
         if (eb & (1ull << 42)) rx = DV_MD(sh1);                        // MOV [s],X
         if (eb & (1ull << 43)) ry = DV_MD(sh2);                        // MOV [s],Y
         if (eb & (2ull << 44)) {
            if (eb & (1ull << 44)) ac.all = (s64)(s32)DV_MD(sh2);       // MOV [s],A
            else ac.all = alu.all;                                      // MOV ALU,A
         } else if (eb & (1ull << 44)) ac.all = 0;                      // CLR A
         inc = (u32)eb;
         goto *(&&d_nix + s_vord[pc]);
      }
      /* ---------- D1 bus (dw <= 0, so "Finish Previous DMA" is not needed) ---------- */
   d_imm_md:   /* MOV SImm,[d]: apply pending incFlg before writing */
      if (inc) DV_CTINC(inc);
      { const u32 dsh = (u32)(eb >> 59); inc = 0xFFu << dsh; DV_MD(dsh) = (u32)(ea >> 32); }
      DV_TAIL_INC();
   d_imm_reg:
      if (inc) DV_CTINC(inc);
      inc = 0; v = (u32)(ea >> 32);
      goto d_reg;
   d_md_md:
      v = DV_MD((u32)(ea >> 32));
      DV_MD((u32)(eb >> 59)) = v;
      DV_TAIL_INC();
   d_md_reg:
      v = DV_MD((u32)(ea >> 32));
      goto d_reg;
   d_sp_md:
   d_sp_reg: {
      const u32 n = (u32)(ea >> 32);
      v = (n == 0x9) ? (u32)alu.part.L : (n == 0xA) ? (u32)(alu.all >> 16) : 0xFFFFFFFFu;
      if (((u32)(eb >> 46) & 7u) == DV_D1_SP_MD) { DV_MD((u32)(eb >> 59)) = v; DV_TAIL_INC(); }
      goto d_reg; }
   d_reg:
      switch ((u32)(eb >> 59)) {
         case 0x4: rx = v; break;
         case 0x5: p.all = (signed)v; break;
         case 0x6: DL->RA0 = v; break;
         case 0x7: DL->WA0 = v; break;
         case 0xA: DL->LOP = (u16)v; break;
         case 0xB: DL->TOP = (u8)v; break;
         case 0xC: case 0xD: case 0xE: case 0xF: DV_CTSET((u32)(eb >> 59) & 3u, v); break;
         default: break;
      }
      DV_TAIL_INC();
   d_nix:
      DV_TAIL_INC();

      /* ---------- classes 2/3/1 ---------- */
   k_mvi:
      inc = 0;
      if (((pcp.all & (u32)eb) != 0) == ((eb >> 40) & 1)) {   /* writeloadimdest (dw <= 0: no step) */
         const u32 d = (u32)(eb >> 59) & 0xF;
         v = (u32)(ea >> 32);
         if (d <= 3) { DL->MD[d][DV_CT(d)] = v; inc = DV_LANE(d); }
         else switch (d) {
            case 0x4: rx = v; break;
            case 0x5: p.all = (s32)v; break;
            case 0x6: DL->RA0 = (v & 0x1FFFFFF); break;
            case 0x7: DL->WA0 = (v & 0x1FFFFFF); break;
            case 0xA: DL->LOP = (u16)(v & 0x0FFF); break;
            case 0xC: DL->TOP = (u8)(pc + 1u); jmp = (s32)v; DL->delayed = 0; break;
            default: break;
         }
      }
      DV_TAIL_INC();
   k_jmp:
      if (jmp == (s32)0xffffffff && ((pcp.all & (u32)eb) != 0) == ((eb >> 40) & 1)) { jmp = (s32)(u32)(ea >> 32); DL->delayed = 0; }
      DV_TAIL0();
   k_lps:
      if (DL->LOP != 0) { jmp = (s32)pc; DL->delayed = 0; DL->LOP--; }
      DV_TAIL0();
   k_btm:
      if (DL->LOP != 0) { jmp = (s32)DL->TOP; DL->delayed = 0; DL->LOP--; }
      else DL->LOP = (u16)((DL->LOP - 1) & 0xFFF);   /* BTM always decrements LOP (same as ScuDspExecInner) */
      DV_TAIL0();
   k_dma: {   /* dw <= 0, so "Finish Previous DMA" is not needed */
      const u32 instruction = (u32)ea;
      DL->dsp_dma_instruction = instruction;
      pcp.part.T0 = 1;
      int Counter = 0;
      if ( ((instruction >> 10) & 0x1F) == 0x00 || ((instruction >> 10) & 0x1F) == 0x04 ||
           ((instruction >> 11) & 0x0F) == 0x08 || ((instruction >> 10) & 0x1F) == 0x14 ) {
         Counter = instruction & 0xFF;
      } else if ( ((instruction >> 11) & 0x0F) == 0x04 || ((instruction >> 10) & 0x1F) == 0x0C ||
                  ((instruction >> 11) & 0x0F) == 0x0C || ((instruction >> 10) & 0x1F) == 0x1C ) {
         const u32 k = instruction & 0x3;
         Counter = DL->MD[k][DV_CT(k)];
         if (instruction & 0x4) DV_CTINC1(k);
      }
      DL->dsp_dma_size = Counter;
      DL->dsp_dma_wait = Counter >> 12; // DMA operation will be start when this count is zero
      DL->WA0M = DL->WA0;
      DL->RA0M = DL->RA0;
      pruef = 1;
      DV_TAIL0(); }
   k_end:
      /* dw <= 0: no open DMA here; only the first-END notification remains.
         All calls in one SYNC/LOAD bracket (cold; no state value lives across a call). */
      pcp.part.EX = 0;
      DV_SYNC();
      if ((u32)ea & 0x8000000) {       // End with Interrupt
         DL->ProgControlPort.part.E = 1;
         ScuSendDSPEnd();
      }
      DV_LOAD();
      pcp.part.P = pc + 1;
      dsp_counter = 1;
      pruef = 1;
      DV_TAIL0();

   end_pc:
      DV_TAIL_INC();
   ende0:
      DV_TAIL0();
   aus:
   DV_SYNC();
}

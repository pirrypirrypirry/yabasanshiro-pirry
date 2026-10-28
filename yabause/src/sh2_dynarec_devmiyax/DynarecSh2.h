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

#ifndef _DYNAREC_SH2_H_
#define _DYNAREC_SH2_H_

#include <list>
#include <vector>
#include <map>
#include <string>
#include <unordered_map>

#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>

#include "debug.h"
#include "threads.h"

//****************************************************
// Defiens
//****************************************************

// instruction Format
#define ZERO_F  0
#define N_F     1
#define M_F     2
#define NM_F    3
#define MD_F    4
#define ND4_F   5
#define NMD_F   6
#define D_F     7 
#define D12_F   8
#define ND8_F   9
#define I_F     10
#define NI_F    11

#ifdef _WINDOWS
#include <Windows.h>
#define ALLOCATE(x) VirtualAlloc(NULL, x, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#define FREEMEM(x,a)	if(x){ VirtualFree(x, a,MEM_RELEASE ); x = NULL;}
#elif defined(ARCH_IS_LINUX)
#include <sys/mman.h>
#define ALLOCATE(x) mmap (NULL, x, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_FILE|MAP_PRIVATE ,-1, 0);
//#define ALLOCATE(x) mmap ((void*)0x6000000, x, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS ,-1, 0);
#define FREEMEM(x,a) munmap(x,a);
#else
#include <sys/mman.h>
#define ALLOCATE(x) mmap (NULL, x, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_FILE|MAP_PRIVATE ,-1, 0);
#define FREEMEM(x,a) munmap(x,a);
//#define ALLOCATE(x)	malloc(x)
//#define FREEMEM(x,a)	if(x){ free(x); x = NULL;}
#endif

const int MAX_INSTSIZE = 0xFFFF+1;

using std::list;
using std::map;
using std::string;

/* Parent table slot, replacing std::list<u32>. Almost all addresses have
   exactly one parent, so up to two parents are stored inline in p_[]; from
   the third on, all entries move into a vector (p_[] is then unused). The
   storage is contiguous in both cases, so begin()/end() are plain pointers.
   remove() deletes all occurrences, matching std::list semantics. */
class ParentSlot {
public:
  ParentSlot() : n_(0), ovf_(0) {}
  ~ParentSlot() { delete ovf_; }

  typedef const u32 * const_iterator;
  /* data() is well defined even for an empty vector. */
  const u32 * begin() const { return ovf_ ? ovf_->data() : p_; }

  /* Unlike std::list iterators, these pointers are invalidated by an insertion
     into the same slot (push_back may reallocate the vector). Callers must not
     insert into a slot while iterating over it. */
  const u32 * end()   const { return begin() + n_; }
  u32  size()  const { return n_; }
  bool empty() const { return n_ == 0; }

  void push_back(u32 v) {
    if (!ovf_) {
      if (n_ < 2) { p_[n_++] = v; return; }
      ovf_ = new std::vector<u32>();
      ovf_->reserve(4);
      ovf_->push_back(p_[0]);
      ovf_->push_back(p_[1]);
    }
    ovf_->push_back(v);
    n_++;
  }

  void remove(u32 v) {
    if (!ovf_) {
      u32 w = 0;
      for (u32 r = 0; r < n_; r++) if (p_[r] != v) p_[w++] = p_[r];
      n_ = w;
      return;
    }
    size_t w = 0;
    for (size_t r = 0; r < ovf_->size(); r++)
      if ((*ovf_)[r] != v) (*ovf_)[w++] = (*ovf_)[r];
    ovf_->resize(w);
    n_ = (u32)w;
  }

  void clear() { n_ = 0; delete ovf_; ovf_ = 0; }

private:
  ParentSlot(const ParentSlot &);
  ParentSlot & operator=(const ParentSlot &);
  u32  p_[2];
  u32  n_;
  std::vector<u32> * ovf_;
};
typedef ParentSlot addrs;

struct CompileStaticsNode {
  u32 time;
  u32 count;
  u32 end_addr;
};

typedef map<u32, CompileStaticsNode> MapCompileStatics;

//****************************************************
// Structs
//****************************************************

/* Block ring: NUMOFBLOCKS blocks of MAXBLOCKSIZE bytes each. The total code
   buffer size is kept roughly constant (twice the blocks at half the size).
   NUMOFBLOCKS must stay a power of two: LastMakeBlock &= (NUMOFBLOCKS-1).
   A block that exceeds MAXBLOCKSIZE is split by the compiler into a follow-up
   block. */
const int NUMOFBLOCKS = 1024*8;
const int MAXBLOCKSIZE = 2048;
#define MAINMEMORY_SIZE (0x100000);
#define ROM_SIZE (0x80000);

struct Block
{
  u8  code[MAXBLOCKSIZE];
  u32 b_addr; //beginning PC
  u32 e_addr; //ending PC
  u32 id;
  u32 flags;
  // No additional fields here: this structure holds the code buffer and is
  // accessed extremely often; changing its size or alignment hurts cache
  // behaviour. Per-block extra data lives in separate tables.
};

/* The assembly stub (dynalib_arm64.s) reads code at offset 0 and flags at 2060. */
static_assert(offsetof(Block, code) == 0, "Block.code must be at offset 0");
static_assert(offsetof(Block, flags) == 2060, "Block.flags must be at offset 2060 (if MAXBLOCKSIZE changed, adjust dynalib_arm64.s)");
#define BLOCK_LOOP  (0x01)
#define BLOCK_WRITE (0x02)
// Marks blocks whose BLOCK_LOOP was set through the two-block adjacency case
// (jump back onto the block lying directly before), as opposed to a
// self-loop within one block.
#define BLOCK_LOOP_ADJ (0x04)

/* Marks blocks whose BLOCK_LOOP was set through the cycle-over-a-jump case,
   as opposed to the adjacency case (cycle through fall-through). A separate
   bit is needed because BLOCK_LOOP_ADJ is set by both cases. */
#define BLOCK_LOOP_CYC (0x08)

#include <cstddef>   /* offsetof */
#define IN_INFINITY_LOOP (-1)

// Sh2 Registris
struct tagSH2
{
  u32 GenReg[16];
  u32 CtrlReg[3];
  u32 SysReg[6];
  uintptr_t getmembyte;
  uintptr_t getmemword;
  uintptr_t getmemlong;
  uintptr_t setmembyte;
  uintptr_t setmemword;
  uintptr_t setmemlong;
  uintptr_t eachclock;
  /* Slice limit read by every block prolog for intra-block back jumps.
     The generated code addresses this structure through computed offsets
     (dynalib_arm64.s), so fields must not be removed or reordered. */
  u32 exitcount;
  /* Resolved base pointers of the work RAM, appended at the end so that all
     existing offsets used in dynalib_arm64.s stay unchanged. */
  u32 inlinemem_on;       /* flag read by the generated code, offset 164; also
                             keeps the following pointers 8-byte aligned */
  uintptr_t hiwram_base;  /* HighWram, resolved  -> offset 168 */
  uintptr_t lowram_base;  /* LowWram,  resolved  -> offset 176 */
  uintptr_t presence_bits; /* CompileBlocks::parent_bits -> offset 184 */
  u32 memcycle;            /* memory access cycles of the running block -> offset 192 */
  u32 memcycle_pad;        /* keeps the following fields 8-byte aligned (offset 196) */
  uintptr_t lookup_hw;     /* &CompileBlocks::LookupTable[0] (HWRAM) -> offset 200 */
  uintptr_t cur_block;     /* last entered block (used by the chaining stub) -> offset 208 */
  uintptr_t chain_target;    /* address of chain_stub -> offset 216 */
};

/* The offsets are hard-coded in dynalib_arm64.s. If the structure changes,
   adjust the assembly, not these checks. */
static_assert(offsetof(tagSH2, getmembyte) == (16+3+7)*4,      "function pointer offset moved");
static_assert(offsetof(tagSH2, inlinemem_on) == (16+3+7)*4+7*8+4, "inlinemem_on must be at offset 164");
static_assert(offsetof(tagSH2, hiwram_base) == (16+3+7)*4+8*8, "hiwram_base must be at offset 168");
static_assert(offsetof(tagSH2, lowram_base) == (16+3+7)*4+9*8, "lowram_base must be at offset 176");
static_assert(offsetof(tagSH2, presence_bits) == (16+3+7)*4+10*8, "presence_bits must be at offset 184");
static_assert(offsetof(tagSH2, memcycle) == (16+3+7)*4+11*8, "memcycle must be at offset 192");
static_assert(offsetof(tagSH2, lookup_hw)  == 200, "lookup_hw must be at offset 200");
static_assert(offsetof(tagSH2, cur_block)  == 208, "cur_block must be at offset 208");
static_assert(offsetof(tagSH2, chain_target) == 216, "chain_target must be at offset 216");
static_assert(offsetof(tagSH2, SysReg) + 5*4 == 96, "ICOUNT (SysReg[5]) must be at offset 96");

// Instruction
struct i_desc
{
  s32 format;
  const char *mnem;
  u16 mask;
  u16 bits;
  u8 dat;
  void (FASTCALL *func)(tagSH2*);
} ;

 
extern i_desc opcode_list[];


inline u32 adress_mask(u32 addr) {
  return (addr & 0x000FFFFF)>>1;
}

// Interrupt Table
struct dIntcTbl
{
  u8 Vector;
  u8 level;
};
typedef std::list<dIntcTbl> dlstIntct;


struct x86op_desc
{
  void(*func)();
  const unsigned short *size;
  const unsigned char *src;
  const unsigned char *dest;
  const unsigned char *off1;
  const unsigned char *imm;
  const unsigned char *off3;
  unsigned char delay;
  unsigned char cycle;
  unsigned char write_count;
  unsigned char build_count;

  x86op_desc(void(*ifunc)(), const unsigned short *isize, const unsigned char *isrc,
    const unsigned char *idest, const unsigned char *ioff1, const unsigned char *iimm,
    const unsigned char *ioff3, const unsigned char idelay, const unsigned char icycle, const unsigned char iwrite_count = 0)
  {
    func = ifunc;
    size = isize;
    src = isrc;
    dest = idest;
    off1 = ioff1;
    imm = iimm;
    off3 = ioff3;
    delay = idelay;
    cycle = icycle;
    write_count = iwrite_count;
    build_count = 0;
  };

};

#define SET_DIRTY
extern "C" {
  /* implemented in dynalib_arm64.s */
  u32 memGetLongFast(u32);
  u16 memGetWordFast(u32);
  u8  memGetByteFast(u32);
  void memSetLongFast(u32, u32);
  void memSetWordFast(u32, u16);
  void memSetByteFast(u32, u8);
  void chain_stub(void); void epilogue_k(void); void PageFlip_k(void);
  void seperator_delay_slot_k(void); void seperator_delay_after_k(void);
  u32 memGetLongFast4(u32);  u16 memGetWordFast4(u32);  u8 memGetByteFast4(u32);
  u32 memGetLongFast5(u32);  u16 memGetWordFast5(u32);  u8 memGetByteFast5(u32);
  void DebugLog(const char * format, ...);
}


class CompileBlocks {
private:
  CompileBlocks(){
    debug_mode_ = false;
    BuildInstructionList();
    dCode = (Block*)ALLOCATE(sizeof(Block)*NUMOFBLOCKS);
#ifdef SET_DIRTY
    LookupParentTable = new addrs[0x100000 >> 1];
    parent_bits = new u32[(0x100000 >> 1) / 32];
    memset(parent_bits, 0, sizeof(u32) * ((0x100000 >> 1) / 32));
#else
    LookupParentTable = NULL;
    parent_bits = NULL;
#endif
    Init();
    compile_count_ = 0;
    exec_count_ = 0;
    remove_count_ = 0;
  }
  ~CompileBlocks(){
    FREEMEM(dCode, sizeof(Block)*NUMOFBLOCKS);
  }
  static CompileBlocks * instance_;
  bool show_code_ = false;
public:
  void setShowCode( bool b ){ show_code_ = b; }
  /* The creation branch is kept out of line (createInstance) so that the
     inlined getInstance() in hot memory access functions stays small. */
  static CompileBlocks * getInstance(){
    if( __builtin_expect(instance_ != NULL, 1) ) return instance_;
    return createInstance();
  }
private:
  static __attribute__((noinline)) CompileBlocks * createInstance();
public:

  int blockCount;
  int LastMakeBlock; 
  bool debug_mode_;
  Block * g_CompleBlock;
  
  u8 dsh2_instructions[MAX_INSTSIZE];
  Block* LookupTable[0x100000>>1];    
  //addrs LookupParentTable[0x100000>>1];
  addrs * LookupParentTable;
  /* Presence bitmap, one bit per address, used as a prefilter for setDirty():
     it is much smaller than LookupParentTable and fits in the cache. A missed
     clear only costs speed; a missed set would skip an invalidation. */
  u32 * parent_bits;   /* 0x80000 Bits = 16384 x u32 */
  inline void PresenceSet(u32 a) { parent_bits[a >> 5] |= (1u << (a & 31)); }
  inline void PresenceClear(u32 a) { parent_bits[a >> 5] &= ~(1u << (a & 31)); }
  inline bool PresenceIsSet(u32 a) const {
    return (parent_bits[a >> 5] & (1u << (a & 31))) != 0;
  }
  /* after remove(): clear the bit only if the list is really empty */
  inline void PresenceAfterRemove(u32 a) {
    if (LookupParentTable[a].size() == 0) PresenceClear(a);
  }
  Block* LookupTableRom[0x80000>>1];
  Block* LookupTableLow[0x100000>>1];
  /* Sized for the full index range (PC & 0x000FFFFF) >> 1, like
     LookupTableLow, so that no PC in the cached 16 MB region indexes past
     the end of the array. */
  Block* LookupTableC[0x100000>>1];
  Block * dCode;
  
  std::unordered_map<u32, int> self_modify_block;

  /* The fast exit stays inline; the rare invalidation work is out of line
     in setDirtySlow() (defined in DynarecSh2.cpp) and must not be inlined
     into the memory write functions. */
  void setDirtySlow(u32 addr);

  inline void setDirty(u32 addr) {
    addr = adress_mask(addr);
    /* prefilter through the presence bitmap */
    if (!PresenceIsSet(addr)) return;
    if (LookupParentTable[addr].size() == 0) return;
    setDirtySlow(addr);
  }

  void Init();

  /* is_slave tells CompileBlock()/EmmitCode() which CPU is being compiled.
     It defaults to false so existing callers (e.g. tests) stay valid. */
  Block * CompileBlock( u32 pc, addrs * ParentT, bool is_slave = false );

  void opcodePass(x86op_desc *op, u16 opcode, u8 *ptr);
  int  opcodeIndex(u16 code );
  void FindOpCode(u16 opcode, u8 * instindex);
  void BuildInstructionList();

  int findFreeBlock(u32 pc);
  int EmmitCode(Block *page, addrs * ParentT = NULL, bool is_slave = false);

  // statics
  u32 compile_count_ ;
  u32 exec_count_;
  u32 remove_count_;


  void ShowStatics();
  void SetDebugMode(bool debug) { debug_mode_ = debug;  }
};

typedef void(*dynaFunc)(tagSH2*);

class DynarecSh2
{
protected:
  SH2_struct *parent;
  tagSH2 *  m_pDynaSh2;
  CompileBlocks * m_pCompiler;
  int       m_ClockCounter;
  bool      m_bIntruptSort;
  bool one_step_;
  u32 pre_exe_count_;
  bool is_slave_ = false;
  /* Block address of the first slave lock in the current time slice
     (kept in the CPU object, not in struct Block). 0 = none yet. */
  u32 fs_first_block_ = 0;
  u32 pre_PC_;
  SH2_struct * ctx_;
  YabMutex * mtx_;
  bool logenable_;

  u32 pre_cnt_;
  u32 interruput_chk_cnt_;
  u32 interruput_cnt_;
  u32 loopskip_cnt_;

  enum enDebugState {
    NORMAL,
    REQUESTED,
    COLLECTING,
    FINISHED,
  };
  
  enDebugState statics_trigger_ = NORMAL;
  MapCompileStatics compie_statics_;
  string message_buf;

public:
  /* Number of times the idle-loop skip fired at run time. */
  u32 GetLoopskipCnt() const { return loopskip_cnt_; }

  DynarecSh2();
  ~DynarecSh2();
  static DynarecSh2 * CurrentContext;
  void SetCurrentContext(){
    CurrentContext = this;
    /* Refresh the base pointers on every CPU switch: the constructor may run
       before the memory is allocated and would store NULL. */
    m_pDynaSh2->hiwram_base = (uintptr_t)HighWram;
    m_pDynaSh2->lowram_base = (uintptr_t)LowWram;
    m_pDynaSh2->presence_bits = (uintptr_t)CompileBlocks::getInstance()->parent_bits;
    m_pDynaSh2->lookup_hw  = (uintptr_t)&CompileBlocks::getInstance()->LookupTable[0];
    m_pDynaSh2->chain_target = (uintptr_t)chain_stub;
  }
  void SetSlave(bool is_slave) { is_slave_ = is_slave; }
  void SetContext(SH2_struct * ctx) { ctx_= ctx;}
  bool IsSlave() { return is_slave_;  }
  // Credit cycles run by a nested doorbell catch-up to the next regular slice.
  void SyncCredit(u32 n) { pre_exe_count_ += n; }

  dlstIntct m_IntruptTbl;
  void RemoveInterrupt(u8 Vector, u8 level);
  void AddInterrupt( u8 Vector, u8 level );
  int CheckInterupt();
  int InterruptRutine(u8 Vector, u8 level);
  int CheckOneStep();

  void ResetCPU();  
  void ExecuteCount(u32 Count );
  int Execute();
  void Undecoded();

  /* Intentionally a no-op in the dynarec. */
  void AddCycle(u32 cycle) { (void)cycle; }
  /* The memory cycle counter lives in tagSH2 (m_pDynaSh2->memcycle). */
  void ShowStatics();
  void ShowCompileInfo();
  void ResetCompileInfo();

  void onFrame(){
    m_pCompiler->self_modify_block.clear();
  }

  tagSH2 * getDynaSh(){ return m_pDynaSh2; }; 

  inline u32 * GetGenRegPtr() { return m_pDynaSh2->GenReg; }
  inline u32 GET_MACH() { return m_pDynaSh2->SysReg[0]; }
  inline u32 GET_MACL() { return m_pDynaSh2->SysReg[1]; }
  inline u32 GET_PR() { return m_pDynaSh2->SysReg[2]; }
  inline u32 GET_PC() { return m_pDynaSh2->SysReg[3]; }
  inline u32 GET_COUNT() { return m_pDynaSh2->SysReg[4]; } 
  inline u32 GET_ICOUNT() { return m_pDynaSh2->SysReg[5]; } 
  inline u32 GET_SR() { return m_pDynaSh2->CtrlReg[0]; }
  inline u32 GET_GBR() { return m_pDynaSh2->CtrlReg[1]; }
  inline u32 GET_VBR() { return m_pDynaSh2->CtrlReg[2]; }
  inline void SET_MACH( u32 v ) { m_pDynaSh2->SysReg[0] = v; }
  inline void SET_MACL( u32 v ) { m_pDynaSh2->SysReg[1] = v; }
  inline void SET_PR( u32 v ) { m_pDynaSh2->SysReg[2] = v; }
  inline void SET_PC( u32 v ) { m_pDynaSh2->SysReg[3] = v; }
  inline void SET_COUNT( u32 v ) { m_pDynaSh2->SysReg[4] = v; } 
  inline void SET_ICOUNT(u32 v ) { m_pDynaSh2->SysReg[5] = v; } 
  inline void SET_SR(u32 v ) { m_pDynaSh2->CtrlReg[0] = v; }
  inline void SET_GBR( u32 v ) { m_pDynaSh2->CtrlReg[1] = v; }
  inline void SET_VBR( u32 v ) { m_pDynaSh2->CtrlReg[2] = v; }  

  int GetCurrentStatics(MapCompileStatics & buf);
  int Resume();
};


// callback from cpu emulation
extern "C"
{
  int EachClock();
  int DelayEachClock();
  int DebugEachClock();
  int DebugDelayClock();

  void memSetByte(u32, u8 );
  void memSetWord(u32, u16);
  void memSetLong(u32, u32);

  u8 memGetByte(u32);
  u16 memGetWord(u32);
  u32 memGetLong(u32);  
  
}

#ifdef _WINDOWS

#define dynaLock()	__asm \
{                         \
    __asm push edx         \
   /*__asm push ebx*/         \
}
#define dynaFree()	__asm \
{                         \
   /*__asm pop ebx*/          \
   __asm pop edx          \
}

#else
#define dynaLock()
#define dynaFree()
#endif


#endif // _DYNAREC_SH2_H_

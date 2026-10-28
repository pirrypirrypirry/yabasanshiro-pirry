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

; mov	16	レジスタ間のコピー
; ldr	13	メモリからレジスタに読み出し
; b	10	無条件分岐
; bl	9	サブルーチン呼び出し
; add	8	加算
; cmp	6	比較
; str	6	レジスタからメモリへ格納
; ldp	5	スタックから取り出し
; stp	4	スタックへ格納
; adrp	3	レジスタにアドレスを設定
; cbz	3	比較して 0 なら分岐
; b.eq	3	等しければ分岐
; ret	3	サブルーチンから戻る
; sub	2	減算
; b.ne	2	異なれば分岐
; adr	1	レジスタにアドレスを設定
; cbnz	1	比較して非 0 なら分岐
; and	1	ビット積

; w0  ; not keeped on function call
; w1  ; not keeped on function call  
; w2  ; not keeped on function call
; w3  ; not keeped on function call
; w4  
; w5  
; w6


; w19   <- Address of GenReg
; w20   <- PC
; w21   <- ClockCounter

; w22 - 28 safe from fucntion call

; w10  <- 
; w11     ; scrach
; w12 ip
; w13 sp
; w14 lr  ; scrach
; w15 pc

  u32 GenReg[16];
  u32 CtrlReg[3];
  u32 SysReg[6];

  MACH   [x19, #(16+3+0)*4 ]  
  MACL   [x19, #(16+3+1)*4 ]  
  PR     [x19, #(16+3+2)*4 ]  
  PC     [x19, #(16+3+3)*4 ]  
  COUNT  [x19, #(16+3+4)*4 ]  
  ICOUNT [x19, #(16+3+5)*4 ]  
  SR     [x19, #(16+0)*4 ]  
  GBR     [x19, #(16+1)*4 ]  
  VBR     [x19, #(16+2)*4 ]  
  getmembyte = [x19, #(16+3+6+0)*8 ] ;
  getmemword = [x19, #(16+3+6+1)*8 ] ;
  getmemlong = [x19, #(16+3+6+2)*8 ] ;
  setmembyte = [x19, #(16+3+6+3)*8 ] ;
  setmemword = [x19, #(16+3+6+4)*8 ] ;
  setmemlong = [x19, #(16+3+6+5)*8 ] ;

  http://www.mztn.org/slasm/arm07.html
  http://www.peter-cockerell.net/aalp/html/ch-3.html

  union {
    struct {
      u32 T:1;
      u32 S:1;
      u32 reserved0:2;
      u32 I:4;
      u32 Q:1;
      u32 M:1;
      u32 reserved1:22;
    } part;
    u32 all;
  } SR;
 
*/

/*
;Memory Functions
extern _memGetByte, _memGetWord, _memGetLong
extern _memSetByte, _memSetWord, _memSetLong
extern _EachClock, _DelayEachClock, _DebugEachClock, _DebugDelayClock
*/
 
.arch armv8-a
 
.macro opfunc name
.section .text 
  .global x86_\name
  .type	x86_\name, %function
  x86_\name:
.endm

.macro CALL_GETMEM_BYTE 
  ldr x10 , [x19, #(16+3+7)*4+(0*8) ]  
  blr x10
.endm  

.macro CALL_GETMEM_WORD 
  ldr x10 , [x19, #(16+3+7)*4+(1*8) ]  
  blr x10
.endm  

.macro CALL_GETMEM_LONG
  ldr x10 , [x19, #(16+3+7)*4+(2*8) ]  
  blr x10
.endm  


.macro CALL_SETMEM_BYTE 
  ldr x10 , [x19, #(16+3+7)*4+(3*8) ]  
  blr x10
.endm  

.macro CALL_SETMEM_WORD 
  ldr x10 , [x19, #(16+3+7)*4+(4*8) ]  
  blr x10
.endm  

.macro CALL_SETMEM_LONG
  ldr x10 , [x19, #(16+3+7)*4+(5*8) ]  
  blr x10
.endm  

.macro CALL_EACHCLOCK
  STR_PC w20
  ldr x10 , [x19, #(16+3+7)*4+(6*8) ]  
  blr x10
.endm  

.macro LDR_EXIT_COUNT reg
  ldr \reg , [x19, #(16+3+7)*4+(7*8) ]  
.endm  

.macro LDR_MACH reg
  ldr \reg , [x19, #(16+3+0)*4 ]  
.endm  

.macro LDR_MACL reg
  ldr \reg , [x19, #(16+3+1)*4 ]  
.endm  

.macro LDR_PR reg
   ldr \reg , [x19, #(16+3+2)*4 ] 
.endm

.macro LDR_PC reg
   ldr \reg , [x19, #(16+3+3)*4 ] 
.endm

.macro LDR_COUNT reg
   ldr \reg , [x19, #(16+3+4)*4 ] 
.endm

.macro LDR_SR reg
  mov \reg, w26
  //ldr   \reg , [x19, #(16+0)*4 ]  
.endm

.macro LDR_SR_REG reg
  ldr   \reg , [x19, #(16+0)*4 ]  
  //nop
.endm


.macro LDR_GBR reg
   ldr \reg , [x19, #(16+1)*4 ] 
.endm

.macro LDR_VBR reg
   ldr \reg , [x19, #(16+2)*4 ] 
.endm

.macro STR_MACH reg
  str \reg , [x19, #(16+3+0)*4 ]  
.endm  

.macro STR_MACL reg
  str \reg , [x19, #(16+3+1)*4 ]  
.endm  

.macro STR_PR reg
  str \reg , [x19, #(16+3+2)*4 ] 
.endm

.macro STR_PC reg
  str \reg , [x19, #(16+3+3)*4 ] 
.endm

.macro STR_COUNT reg
  str \reg , [x19, #(16+3+4)*4 ] 
.endm

.macro STR_SR reg
   //str \reg , [x19, #(16+0)*4 ] 
   mov w26, \reg 
.endm

.macro STR_SR_REG reg
  //nop 
  str \reg , [x19, #(16+0)*4 ] 
.endm


.macro STR_GBR reg
  str \reg , [x19, #(16+1)*4 ] 
.endm

.macro STR_VBR reg
  str \reg , [x19, #(16+2)*4 ] 
.endm


.macro opdesc name,b,c,d,e,f,g
.section .data
  .global \name\()_size
  \name\()_size: .long \b
  .global \name\()_src
   \name\()_src: .long \c
  .global \name\()_dest
   \name\()_dest: .long \d
  .global \name\()_off1
   \name\()_off1: .long \e
  .global \name\()_imm
   \name\()_imm: .long \f
  .global \name\()_off3
   \name\()_off3: .long \g
.endm  

.macro ctrlreg_load a     // 5
  add w0,w10, (\a*4)      // 2
  ldr	w0, [x0]
.endm

//=====================================================
// Basic
//=====================================================

.text
.align  4

//-----------------------------------------------------
// Begining of block
// w7   <- Address of GenReg
// w20   <- PC
// w9   <- Clock Counter
.global prologue
prologue:
//push {w4-w10, lr}   // push regs
sub  sp, sp, #80
stp  x19, x20, [sp]  
stp  x21, x22, [sp,#16]
stp  x23, x24, [sp,#32]
stp  x29, x30, [sp,#48] 
stp  x25, x26, [sp,#64]
mov x19, x0      // GenReg( w0 has adress of m_pDynaSh2)
LDR_PC w20       // PC
LDR_COUNT w21    // ClockCounter
LDR_EXIT_COUNT w25 // Target Count
LDR_SR_REG w26
.size	prologue, .-prologue // 8*4


//-----------------------------------------------------
// normal part par instruction 
.global seperator_normal
seperator_normal:
add w20, w20, #2    // 3 PC += 2
add w21, w21, #1    // 4 Clock += 1  
.size	seperator_normal, .-seperator_normal // 8

//----------------------------------------------------- 
// Delay slot part par instruction
.global seperator_delay_slot
seperator_delay_slot:
cbnz w0,continue
add w20, w20, #2    // PC += 2
STR_PC w20
add w21, w21, #1    // Clock += 1  
STR_COUNT w21
STR_SR_REG w26
ldp  x19, x20, [sp]  
ldp  x21, x22, [sp,#16]
ldp  x23, x24, [sp,#32]
ldp  x29, x30, [sp,#48] 
ldp  x25, x26, [sp,#64]
add  sp, sp, #80
ret
continue:
mov w20, w0    // copy jump addr
sub w20, w20, #2    // PC -= 2
.size seperator_delay_slot, .-seperator_delay_slot // 40
  

//-----------------------------------------------------
// Delay slot part par instruction
.global seperator_delay_after
seperator_delay_after:
add w20, w20, #2    // PC += 2
STR_PC w20     // store to memory
add w21, w21, #1    // Clock += 1  
STR_COUNT w21  // store to memory
STR_SR_REG w26
ldp  x19, x20, [sp]  
ldp  x21, x22, [sp,#16]
ldp  x23, x24, [sp,#32]
ldp  x29, x30, [sp,#48] 
ldp  x25, x26, [sp,#64]
add  sp, sp, #80
ret
.size seperator_delay_after, .-seperator_delay_after // 20

.global internal_delay_jmp
internal_delay_jmp:
add w20, w20, #2    // PC += 2
cmp w21, w25
add w21, w21, #1    // Clock += 1  
b.hi internal_delay_jmp.finish
b #0
internal_delay_jmp.finish:
STR_PC w20     // store to memory
STR_COUNT w21  // store to memory
STR_SR_REG w26
ldp  x19, x20, [sp]  
ldp  x21, x22, [sp,#16]
ldp  x23, x24, [sp,#32]
ldp  x29, x30, [sp,#48] 
ldp  x25, x26, [sp,#64]
add  sp, sp, #80
ret
internal_delay_jmp.next:
//internal_delay_jmp_size = .-internal_delay_jmp
//-------------------------------------------------------
// End of block
.global epilogue
epilogue:
STR_PC w20     // store PC to memory
STR_COUNT w21  // store COUNTER to memory
STR_SR_REG W26
ldp  x19, x20, [sp]  
ldp  x21, x22, [sp,#16]
ldp  x23, x24, [sp,#32]
ldp  x29, x30, [sp,#48] 
ldp  x25, x26, [sp,#64]
add  sp, sp, #80
ret
.size	epilogue, .-epilogue // 12

//-----------------------------------------------------
// Jump part 20
.global PageFlip
PageFlip:
//cmn w0, #1 // 7
//bne PageFlip.jmp     // 2
cbz w0,PageFlip.next
STR_PC w0     // store PC to memory
STR_COUNT w21  // store COUNTER to memory
STR_SR_REG w26
ldp  x19, x20, [sp]  
ldp  x21, x22, [sp,#16]
ldp  x23, x24, [sp,#32]
ldp  x29, x30, [sp,#48] 
ldp  x25, x26, [sp,#64]
add  sp, sp, #80
ret
PageFlip.next:
.size	PageFlip, .-PageFlip // 22

.global internal_jmp
internal_jmp:
cmp w21,w25
b.hi internal_jmp.finish
cbz w0,internal_jmp.next
mov w20, w0
b #0
internal_jmp.finish:
cbnz w0,internal_jmp.jmp
STR_PC w20     // store PC to memory
b internal_jmp.finishfinish
internal_jmp.jmp:
STR_PC w0
internal_jmp.finishfinish:
STR_COUNT w21  // store COUNTER to memory
STR_SR_REG w26
ldp  x19, x20, [sp]  
ldp  x21, x22, [sp,#16]
ldp  x23, x24, [sp,#32]
ldp  x29, x30, [sp,#48] 
ldp  x25, x26, [sp,#64]
add  sp, sp, #80
ret
internal_jmp.next:





//-------------------------------------------------------
// normal part par instruction( for debug build )
.global seperator_d_normal
seperator_d_normal:
add w20, w20, #2    // PC += 2
add w21, w21, #1    // Clock += 1  
STR_PC w20     // store to memory
STR_COUNT w21  // store to memory
STR_SR_REG w26
mov w23,w0
CALL_EACHCLOCK
cmp w0, #1
b.ne seperator_d_normal.continue
ldp  x19, x20, [sp]  
ldp  x21, x22, [sp,#16]
ldp  x23, x24, [sp,#32]
ldp  x29, x30, [sp,#48] 
ldp  x25, x26, [sp,#64]
add  sp, sp, #80
ret
seperator_d_normal.continue:
mov w0,w23

//------------------------------------------------------
// Delay slot part par instruction( for debug build )
.global seperator_d_delay
seperator_d_delay:
mov w22,w0
STR_PC w20     // store to memory
STR_COUNT w21  // store to memory
STR_SR_REG w26
CALL_EACHCLOCK
cbnz w22,seperator_d_delay.continue
add w20, w20, #2    // PC += 2
STR_PC w20
add w21, w21, #1    // Clock += 1  
STR_COUNT w21
ldp  x19, x20, [sp]  
ldp  x21, x22, [sp,#16]
ldp  x23, x24, [sp,#32]
ldp  x29, x30, [sp,#48] 
ldp  x25, x26, [sp,#64]
add  sp, sp, #80
ret
seperator_d_delay.continue:
mov w20, w22    // copy jump addr
sub w20, w20, #2    // PC -= 2
.size seperator_delay_slot, .-seperator_delay_slot // 40


opdesc CLRT, (1*4),0xff,0xff,0xff,0xff,0xff
opfunc CLRT
bic    w26,w26, #1


opdesc CLRMAC,24,0xff,0xff,0xff,0xff,0xff
opfunc CLRMAC
LDR_MACH w0
mov      w0, #0
STR_MACH w0
LDR_MACL w0
mov      w0, #0
STR_MACL w0

opdesc NOP,		4,0xff,0xff,0xff,0xff,0xff
opfunc NOP
nop

opdesc SETT,	(1*4),0xff,0xff,0xff,0xff,0xff
opfunc SETT
orr w26, w26 , #1

opdesc SLEEP,	4,0xff,0xff,0xff,0xff,0xff
opfunc SLEEP
sub w20, w20, #2

opdesc SWAP_W,	20,0,4,0xff,0xff,0xff
opfunc SWAP_W
mov w0, #0 // m
mov w1, #0 // n
ldr w2, [x19, x0]
ror w2, w2, #16
str w2, [x19, x1]

opdesc SWAP_B,	(10*4),0,4,0xff,0xff,0xff
opfunc SWAP_B
mov w0, #0 // m
mov w1, #0 // n
ldr w0, [x19, x0]
lsl w2, w0, #8
lsl w3, w0, #16
uxth  w2, w2
lsr w0, w0, #16
orr w3, w2, w3, lsr #24
orr w3, w3, w0, lsl #16
str w3, [x19, x1]
 



REV w3, w2
str w3, [x19, x1]

opdesc TST,	(7*4),0,4,0xff,0xff,0xff
opfunc TST
mov w0, #0 // m
mov w1, #0 // n
ldr     w1, [x19, x1]
ldr     w3, [x19, x0]
tst     w1, w3
cset    w4, eq		/* branch-free T-bit update */
bfi     w26, w4, #0, #1


opdesc TSTI,	(7*4),0xff,0xff,0xff,0,0xff
opfunc TSTI
mov     w0, #0   // n
ldr     w3, [x19] // R[0]
tst     w0, w3
b.eq    TSTI_EQ
bic     w26, w26, #0x1
b       TSTI_FINISH
TSTI_EQ:
orr     w26, w26, #0x1
TSTI_FINISH:

opdesc ANDI,	16,0xff,0xff,0xff,0,0xff
opfunc ANDI
mov  w0, #0
ldr  w3, [x19]
and  w0, w0, w3
str  w0, [x19]

opdesc XORI,	16,0xff,0xff,0xff,0,0xff
opfunc XORI
mov  w0, #0
ldr  w3, [x19]
eor  w0, w3, w0
str  w0, [x19]

opdesc ORI,	16,0xff,0xff,0xff,0,0xff
opfunc ORI
mov  w0, #0
ldr  w3, [x19]
orr  w0, w0, w3
str  w0, [x19]

opdesc XTRCT,	(6*4),0,4,0xff,0xff,0xff
opfunc XTRCT
mov w0, #0 // m
mov w1, #0 // n
ldr     w3, [x19, x1]
ldr     w0, [x19, x0]
extr    w3, w0, w3, #16
str     w3, [x19, x1]

opdesc ADD,		24,0,4,0xff,0xff,0xff
opfunc ADD
mov w1, #0 // b
mov w0, #0 // c
ldr w1, [x19, x1]
ldr w3, [x19, x0]
add w3, w3, w1
str w3, [x19, x0]

opdesc ADDC,	(14*4),0,4,0xff,0xff,0xff
opfunc ADDC
mov w0, #0
mov w1, #0
mov w5, 1
and     w4, w26, w5                                                      
ldr     w6, [x19,x1]                                              
ldr     w0, [x19,x0]                                              
add     w0, w6, w0                                                      
add     w4, w0, w4                                                      
str     w4, [x19,x1]                                              
cmp     w0, w4                                                          
bhi     ADDC.L10                                                            
cmp     w6, w0                                                          
cset    w5, hi                                                          
ADDC.L10:                                                                           
bfi     w26, w5, 0, 1                                                    

opdesc ADDV, (14*4),0,4,0xff,0xff,0xff
opfunc ADDV
mov w0, #0 // source
mov w1, #0 // dest
and     w2, w26, -2                                                      
ldr     w3, [x19,x1]                                              
ldr     w0, [x19,x0]                                              
add     w5, w3, w0                                                      
lsr     w3, w3, 31                                                      
add     w0, w3, w0, lsr 31                                              
str     w5, [x19,x1]                                              
add     w3, w3, w5, lsr 31                                              
cmp     w3, 1                                                           
cset    w1, eq                                                          
bic     w0, w1, w0                                                      
orr     w26, w2, w0 



opdesc SUBV, (16*4),0,4,0xff,0xff,0xff
opfunc SUBV
mov w0, #0 // source
mov w1, #0 // dest
and     w2, w26, -2                                                      
ldr     w4, [x19,x0]                                              
ldr     w0, [x19,x1]                                              
sub     w5, w0, w4                                                      
lsr     w0, w0, 31                                                      
add     w4, w0, w4, lsr 31                                              
str     w5, [x19,x1]                                              
add     w0, w0, w5, lsr 31                                              
cmp     w0, 1                                                           
cset    w1, eq                                                          
cmp     w4, 1                                                           
cset    w0, eq                                                          
and     w0, w1, w0                                                      
orr     w26, w2, w0 

opdesc SUBC,	(14*4),0,4,0xff,0xff,0xff
opfunc SUBC
mov w0, #0 // source
mov w1, #0 // dest
mov     w5, 1                                                           
and     w4, w26, w5                                                      
ldr     w6, [x19,x1]                                              
ldr     w0, [x19,x0]                                              
sub     w0, w6, w0                                                      
sub     w4, w0, w4                                                      
str     w4, [x19,x1]                                              
cmp     w0, w4                                                          
bcc     SUBC.L3                                                             
cmp     w6, w0                                                          
cset    w5, cc                                                          
SUBC.L3:                                                                            
bfi     w26, w5, 0, 1 


opdesc SUB,		24,0,4,0xff,0xff,0xff
opfunc SUB
mov w1, #0 // b
mov w0, #0 // c
ldr w1, [x19, x1]
ldr w3, [x19, x0]
sub w3, w3, w1
str w3, [x19, x0]


opdesc NOT,		20,0,4,0xff,0xff,0xff
opfunc NOT
mov w0, #0 // m
mov w1, #0 // n
ldr  w3, [x19, x0]
mvn  w3, w3
str  w3, [x19, x1]

opdesc NEG,		20,0,4,0xff,0xff,0xff
opfunc NEG
mov w0, #0 // m
mov w1, #0 // n
ldr  w3, [x19,x0]
neg  w3, w3
str  w3, [x19,x1]

opdesc NEGC,	(14*4),0,4,0xff,0xff,0xff
opfunc NEGC
mov w0, #0 // source
mov w1, #0 // dest
ldr	w2, [x19,x0]
and	w4, w26, 1
orr	w5, w26, 1
neg	w2, w2
and	w26, w26, -2
sub	w4, w2, w4
cmp	w2, wzr
csel	w26, w26, w5, eq
str	w4, [x19,x1]
cmp	w2, w4
bcs	NEGC_END
orr	w26, w26, 1
NEGC_END:

opdesc EXTUB,	(5*4),0,4,0xff,0xff,0xff
opfunc EXTUB
mov w0, #0 // m
mov w1, #0 // n
ldr	w0, [x19,x0]
and	w0, w0, 255
str	w0, [x19,x1]

opdesc EXTU_W,	20,0,4,0xff,0xff,0xff
opfunc EXTU_W
mov w0, #0 // m
mov w1, #0 // n
ldr  w3, [x19,x0]
uxth w3, w3
str  w3, [x19,x1]

opdesc EXTS_B,	20,0,4,0xff,0xff,0xff
opfunc EXTS_B
mov w0, #0 // m
mov w1, #0 // n
ldr  w3, [x19, x0]
sxtb w3, w3
str  w3, [x19, x1]

opdesc EXTS_W,	20,0,4,0xff,0xff,0xff
opfunc EXTS_W
mov w0, #0 // m
mov w1, #0 // n
ldr  w3, [x19, x0]
sxth w3, w3
str  w3, [x19, x1]

//Store Register Opcodes
//----------------------

opdesc STC_SR_MEM,	(7*4),0xff,0,0xff,0xff,0xff
opfunc STC_SR_MEM
mov w2, #0  // n
ldr w0, [x19, x2] // R[n]
sub w0, w0, #4       // 
str w0, [x19, x2] // R[n] -= 4
LDR_SR w1
CALL_SETMEM_LONG


opdesc STC_GBR_MEM,	(7*4),0xff,0,0xff,0xff,0xff
opfunc STC_GBR_MEM
mov w2, #0  // n
ldr w0, [x19, x2] // R[n]
sub w0, w0, #4       // 
str w0, [x19, x2] // R[n] -= 4
LDR_GBR w1
CALL_SETMEM_LONG

opdesc STC_VBR_MEM,	28,0xff,0,0xff,0xff,0xff
opfunc STC_VBR_MEM
mov w2, #0  // n
ldr w0, [x19, x2] // R[n]
sub w0, w0, #4       // 
str w0, [x19, x2] // R[n] -= 4
LDR_VBR w1
CALL_SETMEM_LONG

opdesc MOVBL,	28,0,4,0xff,0xff,0xff
opfunc MOVBL
mov  w0, #0  // m
mov  w22, #0  // n
ldr  w0, [x19, x0] // w0 = R[m] 
CALL_GETMEM_BYTE
sxtb w0, w0
str  w0, [x19, x22]


opdesc MOVWL,		28,0,4,0xff,0xff,0xff
opfunc MOVWL
mov  w0, #0  // m
mov  w22, #0  // n
ldr  w0, [x19, x0] // w0 = R[m] 
CALL_GETMEM_WORD
sxth w0, w0
str  w0, [x19, x22]


opdesc MOVL_MEM_REG, (6*4),0,4,0xff,0xff,0xff
opfunc MOVL_MEM_REG
mov w1,  #0  // c
mov w22,  #0  // b
ldr w0, [x19, x1]
CALL_GETMEM_LONG
str w0, [x19, x22]


opdesc MOVBP,	(9*4),0,4,0xff,0xff,0xff
opfunc MOVBP
mov  w22, #0 // m
mov  w23, #0 // n
ldr	w0, [x19,x22]
add	w1, w0, 1
str	w1, [x19,x22]
CALL_GETMEM_BYTE
sxtb w0, w0
str	w0, [x19,x23]


opdesc MOVWP,	(9*4),0,4,0xff,0xff,0xff
opfunc MOVWP
mov  w22, #0 // m
mov  w23, #0 // n
ldr	w0, [x19,x22]
add	w1, w0, 2
str	w1, [x19,x22]
CALL_GETMEM_WORD
sxth	w0, w0
str	w0, [x19,x23]


opdesc MOVLP,	(8*4),0,4,0xff,0xff,0xff
opfunc MOVLP
mov  w22, #0 // m
mov  w23, #0 // n
ldr	w0, [x19,x22]
add	w1, w0, 4
str	w1, [x19,x22]
CALL_GETMEM_LONG
str	w0, [x19,x23]

opdesc MOVI,	16,0xff,0,0xff,4,0xff
opfunc MOVI
mov w1, #0  // n
mov w0, #0  // i
sxtb w0, w0 // (signed char)i
str  w0, [x19, x1] // R[n] = (int)i

//----------------------

opdesc MOVBL0,	(9*4),0,4,0xff,0xff,0xff
opfunc MOVBL0
mov w1,  #0  
mov w22 , #0  
ldr w0, [x19, x1]
ldr w1, [x19]
add w0, w0, w1
CALL_GETMEM_BYTE
sxtb w0,w0
str w0, [x19, x22]


opdesc MOVWL0,	(9*4),0,4,0xff,0xff,0xff
opfunc MOVWL0
mov w1,   #0  
mov w22 , #0  
ldr w0, [x19, x1]
ldr w1, [x19]
add w0, w0, w1
CALL_GETMEM_WORD
sxth w0,w0
str w0, [x19, x22]


opdesc MOVLL0,	(8*4),0,4,0xff,0xff,0xff
opfunc MOVLL0
mov w1,  #0  
mov w22 , #0  
ldr w0, [x19, x1]
ldr w1, [x19]
add w0, w0, w1
CALL_GETMEM_LONG
str w0, [x19, x22]


opdesc MOVT,	(3*4),0xff,0x0,0xff,0xff,0xff 
opfunc MOVT
mov w0,  #0  
and w1, w26, #1
str w1, [x19, x0];

opdesc MOVBS0,	(8*4),0x0,0x4,0xff,0xff,0xff
opfunc MOVBS0
mov w1,  #0  
mov w0 , #0  
ldr w1, [x19, x1]
ldr w0, [x19, x0]
ldr w2, [x19]
add w0, w0, w2
CALL_SETMEM_BYTE


opdesc MOVWS0,	(8*4),0x0,0x4,0xff,0xff,0xff
opfunc MOVWS0
mov w1,  #0  
mov w0 , #0  
ldr w1, [x19, x1]
ldr w0, [x19, x0]
ldr w2, [x19]
add w0, w0, w2
CALL_SETMEM_WORD

opdesc MOVLS0,	(8*4),0x0,0x4,0xff,0xff,0xff
opfunc MOVLS0
mov w1,  #0  
mov w0 , #0  
ldr w1, [x19, x1]
ldr w0, [x19, x0]
ldr w2, [x19]
add w0, w0, w2
CALL_SETMEM_LONG

//===========================================================================
//Verified Opcodes
//===========================================================================

opdesc DT,		(8*4),0xff,0,0xff,0xff,0xff
opfunc DT
mov     w0, #0            // n
ldr	w2, [x19,x0]
sub	w2, w2, #1
str	w2, [x19,x0]
cbz	w2, DTZERO
and	w26, w26, -2
b DTFINISH
DTZERO:
orr	w26, w26, 1
DTFINISH:


opdesc CMP_PZ,	(6*4),0xff,0,0xff,0xff,0xff
opfunc CMP_PZ
mov     w0, #0
ldr     w0, [x19, x0]
tbnz	w0, #31, CMP_PZ_TRUE
orr	w26, w26, 1
b CMP_PZ_FINISH
CMP_PZ_TRUE:
and	w26, w26, -2
CMP_PZ_FINISH:

opdesc CMP_PL,	(7*4),0xff,0,0xff,0xff,0xff
opfunc CMP_PL
mov     w0, #0
ldr w0, [x19, x0]
cmp	w0, wzr
ble	CMP_PL_TRUE
orr	w26, w26, 1
b CMP_PL_FINISH
CMP_PL_TRUE:
and	w26, w26, -2
CMP_PL_FINISH:

opdesc CMP_EQ,	(7*4),0,4,0xff,0xff,0xff
opfunc CMP_EQ
mov     w0, #0
mov     w1, #0
ldr w4, [x19, x1]
ldr w0, [x19, x0]
cmp	w4, w0
cset	w3, eq		/* branch-free T-bit update */
bfi	w26, w3, #0, #1

opdesc CMP_HS,	(7*4),0,4,0xff,0xff,0xff
opfunc CMP_HS
mov     w0, #0
mov     w1, #0
ldr     w4, [x19, x1]
ldr     w0, [x19, x0]
cmp	w4, w0
cset	w3, cs		/* branch-free T-bit update */
bfi	w26, w3, #0, #1

opdesc CMP_HI,	(7*4),0,4,0xff,0xff,0xff
opfunc CMP_HI
mov     w0, #0
mov     w1, #0
ldr     w4, [x19, x1]
ldr     w0, [x19, x0]
cmp	w4, w0
cset	w3, hi		/* branch-free T-bit update */
bfi	w26, w3, #0, #1

opdesc CMP_GE,	(7*4),0,4,0xff,0xff,0xff
opfunc CMP_GE
mov     w0, #0
mov     w1, #0
ldr     w4, [x19, x1]
ldr     w0, [x19, x0]
cmp	w4, w0
cset	w3, ge		/* branch-free T-bit update */
bfi	w26, w3, #0, #1


opdesc CMP_GT,	(7*4),0,4,0xff,0xff,0xff
opfunc CMP_GT
mov     w0, #0
mov     w1, #0
ldr     w4, [x19, x1]
ldr     w0, [x19, x0]
cmp	w4, w0
cset	w3, gt		/* branch-free T-bit update */
bfi	w26, w3, #0, #1

opdesc CMP_EQ_IMM,	(7*4),0xff,0xff,0xff,0,0xff
opfunc CMP_EQ_IMM
mov w0, #0 // imm
ldr     w3, [x19]
cmp     w3, w0,sxtb
beq	CMP_EQ_IMM_TRUE
and	w26, w26, -2
b CMP_EQ_IMM_FINISH
CMP_EQ_IMM_TRUE:
orr	w26, w26, 1
CMP_EQ_IMM_FINISH:


// string cmp
opdesc CMPSTR, (16*4),0,4,0xff,0xff,0xff
opfunc CMPSTR
mov w0, #0 // m
mov w1, #0 // n
ldr w1, [x19, x1] // R[n]
ldr w0, [x19, x0] // R[m]
eor	w0, w1, w0
lsr	w1, w0, 24
cbz	w1, CMPSTR_TRUE
ubfx	x1, x0, 16, 8
cbz	w1, CMPSTR_TRUE
ubfx	x1, x0, 8, 8
cbz	w1, CMPSTR_TRUE
and	w0, w0, 255
cbz	w0, CMPSTR_TRUE
and	w26, w26, -2
b CMPSTR_FINISH
CMPSTR_TRUE:
orr	w26, w26, 1
CMPSTR_FINISH:


//http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0489fj/CIHDDCIF.html

opdesc ROTL,	(5*4),0xff,0,0xff,0xff,0xff
opfunc ROTL
mov w0, #0   // n
ldr	w1, [x19,x0]
bfxil	w26, w1, #31, #1
ror	w1, w1, #31
str	w1, [x19,x0]

opdesc ROTR,	(5*4),0xff,0,0xff,0xff,0xff
opfunc ROTR
mov w0, #0   // n
ldr	w1, [x19,x0]
bfxil	w26, w1, #0, #1
ror	w1, w1, #1
str	w1, [x19,x0]


opdesc ROTCL,	(6*4),0xff,0,0xff,0xff,0xff
opfunc ROTCL
mov w0, #0   // n
ldr	w1, [x19,x0]
lsl	w2, w1, 1
bfxil	w2, w26, #0, #1
str	w2, [x19,x0]
bfxil	w26, w1, #31, #1


opdesc ROTCR,	(6*4),0xff,0,0xff,0xff,0xff
opfunc ROTCR
mov w0, #0   // n
ldr	w1, [x19,x0]
lsr	w2, w1, 1
bfi	w2, w26, #31, #1
str	w2, [x19,x0]
bfxil	w26, w1, #0, #1


opdesc SHL,		(5*4),0xff,0,0xff,0xff,0xff
opfunc SHL
mov w0, #0 
ldr	w2, [x19,x0]
bfxil	w26, w2, #31, #1
lsl	w2, w2, 1
str	w2, [x19,x0]


opdesc SHLR,	(5*4),0xff,0,0xff,0xff,0xff
opfunc SHLR
mov w0, #0 
ldr	w2, [x19,x0]
bfxil	w26, w2, #0, #1
lsr	w2, w2, 1
str	w2, [x19,x0]


opdesc SHAR,	(5*4),0xff,0,0xff,0xff,0xff
opfunc SHAR
mov w0, #0 
ldr	w1, [x19,x0]
bfxil	w26, w1, #0, #1
asr	w1, w1, 1
str	w1, [x19,x0]


opdesc SHLL2,	16,0xff,0,0xff,0xff,0xff
opfunc SHLL2
mov w0, #0 
ldr w2, [x19, x0]
lsl w2, w2, #2
str w2, [x19, x0]

opdesc SHLR2,	16,0xff,0,0xff,0xff,0xff
opfunc SHLR2
mov w0, #0 
ldr w2, [x19, x0]
lsr w2, w2,  #2
str w2, [x19, x0]

opdesc SHLL8,	16,0xff,0,0xff,0xff,0xff
opfunc SHLL8
mov w0, #0 
ldr w2, [x19, x0]
lsl w2, w2,  #8
str w2, [x19, x0]

opdesc SHLR8,	16,0xff,0,0xff,0xff,0xff
opfunc SHLR8
mov w0, #0 
ldr w2, [x19, x0]
lsr w2, w2,  #8
str w2, [x19, x0]


opdesc SHLL16,	16,0xff,0,0xff,0xff,0xff
opfunc SHLL16
mov w0, #0 
ldr w2, [x19, x0]
lsl w2, w2, #16
str w2, [x19, x0]

opdesc SHLR16,	16,0xff,0,0xff,0xff,0xff
opfunc SHLR16
mov w0, #0 
ldr w2, [x19, x0]
lsr w2, w2, #16
str w2, [x19, x0]


opdesc AND,		24,0,4,0xff,0xff,0xff
opfunc AND
mov w0, #0
mov w1, #0
ldr w3, [x19, x1]
ldr w0, [x19, x0]
and w3, w3, w0
str w3, [x19, x1]

opdesc OR,		24,0,4,0xff,0xff,0xff
opfunc OR
mov w0, #0
mov w1, #0
ldr w3, [x19, x1]
ldr w0, [x19, x0]
orr w3, w3, w0
str w3, [x19, x1]


opdesc XOR,		(6*4),0,4,0xff,0xff,0xff
opfunc XOR
mov w0, #0  // get INSTRUCTION_C
mov w2, #0  // get INSTRUCTION_B
ldr w0,[x19,x0]   
ldr w1,[x19,x2]   
eor w0, w0, w1        // xor  
str w0,[x19,x2]   // R[B] = xor

opdesc ADDI,	(5*4),0xff,4,0xff,0,0xff
opfunc ADDI
mov w0, #0 // w0 = cd
mov w1, #0 // w1 = b
ldr w3, [x19, x1]
add	w0, w3, w0, sxtb
str w0, [x19, x1]

opdesc AND_B,	44,0xff,0xff,0xff,0,0xff
opfunc AND_B
mov     w0, #0     // source
LDR_GBR w23         // GBR
ldr     w24, [x19]   // R[0]
mov     w22, w0     // 
add     w0, w23, w24 // GBR+w0
CALL_GETMEM_BYTE
and     w1, w0, w22  // temp &= source
add     w0, w23, w24  // GBR+w0
CALL_SETMEM_BYTE


opdesc OR_B,	(11*4),0xff,0xff,0xff,0,0xff
opfunc OR_B
mov     w0, #0     // source
LDR_GBR w23         // GBR
ldr     w24, [x19]   // R[0]
mov     w22, w0     // 
add     w0, w23, w24 // GBR+w0
CALL_GETMEM_BYTE
orr     w1, w0, w22  // temp |= source
add     w0, w23, w24  // GBR+w0
CALL_SETMEM_BYTE


opdesc XOR_B,	44,0xff,0xff,0xff,0,0xff
opfunc XOR_B
mov     w0, #0     // source
LDR_GBR w23         // GBR
ldr     w24, [x19]   // R[0]
mov     w22, w0     // 
add     w0, w23, w24 // GBR+w0
CALL_GETMEM_BYTE
eor     w1, w0, w22  // temp ^= source
add     w0, w23, w24  // GBR+w0
CALL_SETMEM_BYTE


opdesc TST_B,	(12*4),0xff,0xff,0xff,(6*4),0xff
opfunc TST_B
ldr     w22, [x19]
LDR_GBR w0
add	w0, w22, w0
CALL_GETMEM_BYTE
uxtb	w0, w0
mov   w1, #0
and   w0,w0,w1
cbz	w0, TST_B.T1
and	w26, w26, -2
b TST_B.FINISH
TST_B.T1:
orr	w26, w26, 1
TST_B.FINISH:

// Jump Opcodes
//------------

opdesc JMP,		8,0xff,0,0xff,0xff,0xff
opfunc JMP
mov w0, #0
ldr w0, [x19,x0]

opdesc JSR,		16,0xff,0,0xff,0xff,0xff
opfunc JSR
mov w0, #0
add w1, w20, #4
STR_PR w1
ldr w0, [x19,x0]

opdesc BRA,		(4*4),0xFF,0xFF,0xFF,0xFF,0
opfunc BRA
mov w0, #0
sbfx	w0, w0, 0, 12
add	w2, w20, #4
add	w0, w2, w0, lsl 1



opdesc BSR,		(5*4),0xFF,0xFF,0xFF,0xFF,0
opfunc BSR
mov w0, #0
sbfx	w0, w0, 0, 12
add w2, w20, #4   // PC += 4
STR_PR w2    // PR(SysReg[2]) = PC
add	w0, w2, w0, lsl 1

opdesc BSRF,		28,0xff,0,0xff,0xff,0xff  // ToDo:
opfunc BSRF
mov w0, #0 // m
ldr w0, [x19, x0]
mov w1, w20
add w1, w1, #4
STR_PR w1
add w0, w0, w20
add w0, w0, #4


opdesc BRAF,		16,0xff,0,0xff,0xff,0xff
opfunc BRAF
mov w0, #0 // m
ldr w0, [x19, x0]
add w0, w0, #4
add w0, w0, w20

opdesc RTS,			4,0xff,0xff,0xff,0xff,0xff
opfunc RTS
LDR_PR w0

opdesc RTE,			(16*4),0xff,0xff,0xff,0xff,0xff
opfunc RTE
ldr     w0, [x19, #60]
CALL_GETMEM_LONG
mov     w22, w0 // PC
ldr     w0, [x19, #60]
add     w0, w0, #4
str     w0, [x19, #60]
CALL_GETMEM_LONG
ldr     w3, [x19, #60]
add     w3, w3, #4
str     w3, [x19, #60]
mov     w1, 1011
and     w0, w0, w1
STR_SR  w0
mov     w0, w22

opdesc TRAPA,	  (17*4),0xff,0xff,0xff,(13*4),0xff
opfunc TRAPA
ldr w0, [x19, #60]  // w0 = R[15]
sub w0, w0, #4         // w0 -= 4
LDR_SR w1          // w1 = SR
CALL_SETMEM_LONG   // MappedMemoryWriteLong(R[15],SR);
ldr w0, [x19, #60]  // w0 = R[15]
sub w0, w0, #8         // w0 -= 8
str w0, [x19, #60]  // R[15] = w0
mov w1, w20         // w1 = PC
add w1, w1, #2         // w1 += 2
CALL_SETMEM_LONG   // MappedMemoryWriteLong(R[15],PC + 2);
LDR_VBR w0         // VBR
mov w1, #0         // w1 = imm
add w0, w0, w1, lsl #2 // PC = MappedMemoryReadLong(VBR+(imm<<2));
CALL_GETMEM_LONG


opdesc BT,		(6*4),0xff,0xff,0xff,0,0xff
opfunc BT
movz w0, #0 // disp
sbfiz	w4, w0, 1, 8
add	w4, w4, w20
add	w4, w4, #4
tst	w26, #1
csel	w0, w4, wzr, ne


opdesc BF,		(6*4),0xff,0xff,0xff,0,0xff
opfunc BF
movz w0, #0 // disp
sbfiz	w4, w0, 1, 8
add	w4, w4, w20
add	w4, w4, #4
tst	w26, #1
csel	w0, w4, wzr, eq


opdesc BF_S,		(6*4),0xFF,0xFF,0xFF,0,0xFF
opfunc BF_S
movz w0, #0 // disp
sbfiz	w4, w0, 1, 8
add	w4, w4, w20
add	w4, w4, #4
tst	w26, #1
csel	w0, w4, wzr, eq


//Store/Load Opcodes
//------------------

opdesc STC_SR,	(3*4),0xff,4,0xff,0xff,0xff
opfunc STC_SR
LDR_SR w1
mov w0, #0
str w1, [x19, x0 ] // R[n] = SR

opdesc STC_GBR,	(3*4),0xff,4,0xff,0xff,0xff
opfunc STC_GBR
LDR_GBR w1
mov w0, #0
str w1, [x19, x0 ] // R[n] = GBR

opdesc STC_VBR,	(3*4),0xff,4,0xff,0xff,0xff
opfunc STC_VBR
LDR_VBR w1
mov w0, #0
str w1, [x19, x0 ] // R[n] = VBR

opdesc STS_MACH, (3*4),0xff,4,0xff,0xff,0xff
opfunc STS_MACH
LDR_MACH w1
mov w0, #0
str w1, [x19, x0 ] // R[n] = MACL

opdesc STS_MACH_DEC, 28,0xff,0,0xff,0xff,0xff
opfunc STS_MACH_DEC
mov w2, #0
ldr w0, [x19, x2]
sub w0, w0, #4
str w0, [x19, x2]
LDR_MACH w1
CALL_SETMEM_LONG


opdesc STS_MACL, (3*4),0xff,4,0xff,0xff,0xff
opfunc STS_MACL
LDR_MACL w1
mov w0, #0
str w1, [x19, x0] // R[n] = MACL

opdesc STS_MACL_DEC, 28,0xff,0,0xff,0xff,0xff
opfunc STS_MACL_DEC
mov w2, #0
ldr w0, [x19, x2]
sub w0, w0, #4
str w0, [x19, x2]
LDR_MACL w1
CALL_SETMEM_LONG

opdesc LDC_SR,	(6*4),0xff,0,0xff,0xff,0xff
opfunc LDC_SR
mov     w1, #0  // m
ldr     w0, [x19, x1] // w5 = R[m] 
bic     w0, w0, #12
lsl     w0, w0, #22
lsr     w0, w0, #22  // SR = w0 & 0w000003f3;
STR_SR  w0

opdesc LDC_SR_INC,	(12*4),0xff,0,0xff,0xff,0xff
opfunc LDC_SR_INC
mov     w1, #0  // m
ldr     w23, [x19, x1] // w23 = R[m] 
mov     w22, w1
mov     w0, w23
CALL_GETMEM_LONG
bic     w0, w0, #12
lsl     w0, w0, #22
lsr     w0, w0, #22  // SR = w0 & 0w000003f3;
STR_SR  w0
add     w23, w23, #4
str     w23, [x19, x22]


opdesc LDCGBR,	12,0xff,0,0xff,0xff,0xff
opfunc LDCGBR
mov     w0, #0  // m
ldr     w0, [x19, x0] // w5 = R[m] 
STR_GBR  w0
 

opdesc LDC_GBR_INC,	(7*4),0xff,0,0xff,0xff,0xff
opfunc LDC_GBR_INC
mov w2, #0 // m
ldr w0, [x19, x2]
add w1, w0 , #4
str w1, [x19, x2]
CALL_GETMEM_LONG
STR_GBR w0 


opdesc LDC_VBR,	12,0xff,0,0xff,0xff,0xff
opfunc LDC_VBR
mov     w0, #0  // m
ldr     w0, [x19, x0] // w5 = R[m] 
STR_VBR  w0

opdesc LDC_VBR_INC,	(7*4),0xff,0,0xff,0xff,0xff
opfunc LDC_VBR_INC
mov w2, #0 // m
ldr w0, [x19, x2]
add w1, w0 , #4
str w1, [x19, x2]
CALL_GETMEM_LONG
STR_VBR w0 


opdesc STS_PR,		12,0xFF,0,0xFF,0xff,0xff
opfunc STS_PR
mov w0, #0
LDR_PR  w1 
str     w1, [x19, x0]

opdesc STSMPR,	(7*4),0xff,0,0xff,0xff,0xff
opfunc STSMPR
// w4 is an ARM64 argument register and is clobbered by CALL_SETMEM_LONG,
// so the callee-saved w22 is used instead.
mov w22, #0
ldr w0, [x19, x22]
sub w0, w0, #4
str w0, [x19, x22]
LDR_PR w1
CALL_SETMEM_LONG

opdesc LDS_PR,		(3*4),0xff,0,0xff,0xff,0xff
opfunc LDS_PR
mov w0, #0 // b
ldr w0, [x19, x0]
STR_PR w0

opdesc LDS_PR_INC,	(7*4),0xff,0,0xff,0xff,0xff
opfunc LDS_PR_INC
mov w2, #0 // m
ldr w0, [x19, x2]
add w1, w0 , #4
str w1, [x19, x2]
CALL_GETMEM_LONG
STR_PR w0       // MACH = ReadLong(sh->regs.R[m]);


opdesc LDS_MACH,		12,0xff,0,0xff,0xff,0xff
opfunc LDS_MACH
mov w0, #0 // b
ldr w0, [x19, x0]
STR_MACH w0


opdesc LDS_MACH_INC,	28,0xff,0,0xff,0xff,0xff
opfunc LDS_MACH_INC
mov w2, #0 // m
ldr w0, [x19, x2]
add w1, w0 , #4
str w1, [x19, x2]
CALL_GETMEM_LONG
STR_MACH w0       // MACH = ReadLong(sh->regs.R[m]);


opdesc LDS_MACL,		12,0xff,0,0xff,0xff,0xff
opfunc LDS_MACL
mov w0, #0 // b
ldr w0, [x19, x0]
STR_MACL w0


opdesc LDS_MACL_INC,	28,0xff,0,0xff,0xff,0xff
opfunc LDS_MACL_INC
mov w2, #0 // m
ldr w0, [x19, x2]
add w1, w0 , #4
str w1, [x19, x2]
CALL_GETMEM_LONG
STR_MACL w0       // MACH = ReadLong(sh->regs.R[m]);

//Mov Opcodes
//-----------

opdesc MOVA,	24,0xff,0xff,0xff,0,0xff
opfunc MOVA
mov     w1, #0  // disp
mov     w2, w20  // PC
add     w2, w2, #4
bic     w2, w2, #3
add     w2, w2, w1, lsl #2
str     w2, [x19]


opdesc MOVWI,	36,0xff,0,0xff,4,0xff
opfunc MOVWI
mov w22, #0 // n
mov w2, #0 // disp
mov w1, w20 // GET PC
add w1, w1, w2, lsl #1 // PC += (disp << 1)
add w0, w1, #4  // PC += 4
CALL_GETMEM_WORD
sxth w0,w0
str w0, [x19, x22] // R[n] = readval

opdesc MOVLI,  36,0xff,0,0xff,4,0xff
opfunc MOVLI
mov w22, #0 // n
mov w2, #0 // disp
mov w1, w20 // GET PC
add w1, w1, #4
bic w1, w1, #3
add w0, w1, w2, lsl #2 // read addr
CALL_GETMEM_LONG
str w0, [x19, x22] // R[n] = readval


opdesc MOVBL4, (8*4),0,0xff,4,0xff,0xff
opfunc MOVBL4
mov w0, #0 // n
mov w1, #0 // disp
ldr w2, [x19, x0 ]
add w0, w2, w1
CALL_GETMEM_BYTE
sxtb w0,w0
str  w0, [x19, #0]

opdesc MOVWL4, (8*4),0,0xff,(2*4),0xff,0xff
opfunc MOVWL4
mov w0, #0 // n
ldr w2, [x19, x0]
mov w1, #0 // disp
add w0, w2, w1, lsl #1
CALL_GETMEM_WORD
sxth w0,w0
str  w0, [x19, #0]

opdesc MOVLL4, (8*4),0,(6*4),(2*4),0xff,0xff
opfunc MOVLL4
mov w0, #0 // n
ldr w2, [x19, x0]
mov w1, #0 // disp
add w0, w2, w1, lsl #2
CALL_GETMEM_LONG
mov w1, #0 // n
str w0, [x19, x1]

opdesc MOVBS4,	28,0,0xff,4,0xff,0xff
opfunc MOVBS4
mov w0, #0 // n
mov w1, #0 // disp
ldr w2, [x19, x0]
add w0, w2, w1
ldr w1, [x19]
CALL_SETMEM_BYTE

opdesc MOVWS4,	28,0,0xff,4,0xff,0xff
opfunc MOVWS4
mov w0, #0 // n
mov w1, #0 // disp
ldr w2, [x19, x0]
add w0, w2, w1, lsl #1
ldr w1, [x19]
CALL_SETMEM_WORD

opdesc MOVLS4,	36,0,8,4,0xff,0xff
opfunc MOVLS4
mov w0, #0 // m
mov w1, #0 // disp
mov w2, #0 // n
ldr w2, [x19, x2]
ldr w3, [x19, x0]
add w0, w2, w1, lsl #2
mov w1, w3
CALL_SETMEM_LONG

opdesc MOVBLG,    28,0xff,0xff,0xff,0,0xff
opfunc MOVBLG
mov w0, #0
LDR_GBR w1
add     w0, w1, w0
CALL_GETMEM_BYTE
sxtb w0,w0
str  w0, [x19]

opdesc MOVWLG,     28,0xff,0xff,0xff,0,0xff
opfunc MOVWLG
mov w0, #0
LDR_GBR w1
add     w0, w1, w0, lsl #1
CALL_GETMEM_WORD
sxth w0,w0
str  w0, [x19]

opdesc MOVLLG,    24,0xff,0xff,0xff,0,0xff
opfunc MOVLLG
mov w0, #0
LDR_GBR w1
add     w0, w1, w0, lsl #2
CALL_GETMEM_LONG
str     w0, [x19]

opdesc MOVBSG,    24,0xff,0xff,0xff,0,0xff
opfunc MOVBSG
mov w0, #0 // disp
LDR_GBR w1 // GBR
add     w0, w0, w1
ldr     w1, [x19]
CALL_SETMEM_BYTE

opdesc MOVWSG,    24,0xff,0xff,0xff,0,0xff
opfunc MOVWSG
mov w0, #0 // disp
LDR_GBR w1 // GBR
add     w0, w1, w0, lsl #1
ldr     w1, [x19]
CALL_SETMEM_WORD

opdesc MOVLSG,    24,0xff,0xff,0xff,0,0xff
opfunc MOVLSG
mov w0, #0 // disp
LDR_GBR w1 // GBR
add     w0, w1, w0, lsl #2
ldr     w1, [x19]
CALL_SETMEM_LONG

opdesc MOVBS,	24,4,0,0xff,0xff,0xff
opfunc MOVBS
mov w0, #0 // b
mov w1, #0 // c
ldr  w0, [x19, x0]
ldrb  w1, [x19, x1]
CALL_SETMEM_BYTE // 2cyclte

        
opdesc MOVWS,	(6*4),4,0,0xff,0xff,0xff
opfunc MOVWS
mov w0, #0 // b
mov w1, #0 // c
ldr  w0, [x19, x0]
ldr  w1, [x19, x1]
CALL_SETMEM_WORD // 2cyclte

opdesc MOVLS,	(6*4),4,0,0xff,0xff,0xff
opfunc MOVLS
mov w0, #0 // b
mov w1, #0 // c
ldr  w0, [x19, x0]
ldr  w1, [x19, x1]
CALL_SETMEM_LONG // 2cyclte

opdesc MOVR,		16,4,0,0xff,0xff,0xff
opfunc MOVR
mov w0, #0 // b
mov w1, #0 // c
ldr  w1, [x19, x1]
str  w1, [x19, x0]


opdesc MOVBM,		32,0,4,0xff,0xff,0xff
opfunc MOVBM
mov w2, #0 // m
mov w3, #0 // n
ldr  w0, [x19, x3] // addr R[n]
ldrb w1, [x19, x2] // val  R[m]
sub  w0, w0, #1
str  w0, [x19, x3] // R[n] -= 1
CALL_SETMEM_BYTE // 2cyclte

opdesc MOVWM,		32,0,4,0xff,0xff,0xff
opfunc MOVWM
mov w2, #0 // m
mov w3, #0 // n
ldr  w0, [x19, x3] // addr R[n]
ldrh w1, [x19, x2] // val  R[m]
sub  w0, w0, #2
str  w0, [x19, x3] // R[n] -= 2
CALL_SETMEM_WORD // 2cyclte

opdesc MOVLM,		32,0,4,0xff,0xff,0xff
opfunc MOVLM
mov w2, #0 // m
mov w3, #0 // n
ldr  w0, [x19, x3] // addr R[n]
ldr  w1, [x19, x2] // val  R[m]
sub  w0, w0, #4
str  w0, [x19, x3] // R[n] -= 4
CALL_SETMEM_LONG // 2cyclte

opdesc TAS,  (15*4),0xff,0,0xff,0xff,0xff
opfunc TAS
mov     w22, #0
ldr     w0, [x19, x22]
CALL_GETMEM_BYTE
uxtb	w0, w0
LDR_SR w3
cbz	w0, TAS.ZERO
and	w3, w3, -2
b TAS.FINISH
TAS.ZERO:
orr	w3, w3, 1 
TAS.FINISH:
STR_SR  w3
orr	w1, w0, 128
ldr	w0, [x19,x22]
CALL_SETMEM_BYTE
      

opdesc DIV0U,	(2*4),0xff,0xff,0xff,0xff,0xff
opfunc DIV0U
//and w0,0xfffffcfe
bic     w26, w26, #768
bic     w26, w26, #1

opdesc DIV0S, (23*4),0,4,0xff,0xff,0xff
opfunc DIV0S
mov w0, #0 // m
mov w1, #0 // n
ldr  w2, [x19, x0 ] // m
ldr  w1, [x19, x1 ] // n
tbnz w1, #31, DIV0S.L2
LDR_SR w1
and      w1, w1, -257
DIV0S.L3:
and	w3, w1, -513
orr	w1, w1, 512
mov w0,w2
cmp	w0, wzr
csel	w0, w1, w3, lt
orr	w3, w0, 1
and	w1, w0, -2
eor	w0, w0, w0, lsr 1
and	w0, w0, 256
cmp	w0, wzr
csel	w0, w1, w3, eq
STR_SR     w0
b DIV0S.FINISH
DIV0S.L2:
LDR_SR  w1
orr	w1, w1, 256
b	DIV0S.L3
DIV0S.FINISH:


opdesc DIV1, (61*4),0,4,0xff,0xff,0xff
opfunc DIV1
mov w0, #0 // m
mov w1, #0 // n
and     w8, w26, 1
ubfx    x6, x26, 8, 1
ubfx    x7, x26, 9, 1
ldr     w3, [x19,x1]
lsr     w5, w3, 31
orr     w3, w8, w3, lsl 1
str     w3, [x19,x1]
cbz     w6, DIV1.L3
// A second "cbz w6" is not needed here: the branch above already leaves
// when w6 is zero, so the flow falls through into DIV1.L2.
ldr     w0, [x19,x0]
cbz     w7, DIV1.L18
sub     w0, w3, w0
str     w0, [x19,x1]
cmp     w3, w0
cset    w0, cc
cmp     w5, wzr
eor     w5, w0, 1
csel    w5, w5, w0, eq
DIV1.L2:
   cmp     w5, w7
   cset    w0, eq
   bfi     w26, w0, 0, 1
   bfi     w26, w5, 8, 1
   b DIV1.FINISH
DIV1.L3:
   ldr     w0, [x19,x0]
   cbz     w7, DIV1.L19
   add     w0, w3, w0
   str     w0, [x19,x1]
   cmp     w3, w0
   cset    w0, hi
   cmp     w5, wzr
   eor     w5, w0, 1
   csel    w5, w5, w0, eq
   cmp     w5, w7
   cset    w0, eq
   bfi     w26, w0, 0, 1
   bfi     w26, w5, 8, 1
   b DIV1.FINISH
DIV1.L19:
   sub     w0, w3, w0
   str     w0, [x19,x1]
   cmp     w3, w0
   cset    w0, cc
   cmp     w5, wzr
   eor     w5, w0, 1
   csel    w5, w5, w0, ne
   cmp     w5, w7
   cset    w0, eq
   bfi     w26, w0, 0, 1
   bfi     w26, w5, 8, 1
   b DIV1.FINISH
DIV1.L18:
   add     w0, w3, w0
   str     w0, [x19,x1]
   cmp     w3, w0
   cset    w0, hi
   cmp     w5, wzr
   eor     w5, w0, 1
   csel    w5, w5, w0, ne
   cmp     w5, w7
   cset    w0, eq
   bfi     w26, w0, 0, 1
   bfi     w26, w5, 8, 1
DIV1.FINISH:

//------------------------------------------------------------
//dmuls
opdesc DMULS, (8*4),0,4,0xff,0xff,0xff
opfunc DMULS
mov w1, #0 // m
mov w0, #0 // n
ldr     w1, [x19, x1]
ldr     w0, [x19, x0]
smull   x0, w1, w0
STR_MACL w0
lsr	x0, x0, 32
STR_MACH w0

//------------------------------------------------------------
//dmulu 32bit -> 64bit Mul
opdesc DMULU, (8*4),0,4,0xff,0xff,0xff
opfunc DMULU
mov w1, #0 // m
mov w0, #0 // n
ldr     w1, [x19, x1]
ldr     w0, [x19, x0]
umull   x0, w1, w0
STR_MACL w0
lsr	x0, x0, 32
STR_MACH w0

//--------------------------------------------------------------
// mull 32bit -> 32bit Multip
opdesc MULL, (6*4),0,4,0xff,0xff,0xff
opfunc MULL
mov w1, #0 // m
mov w0, #0 // n
ldr     w2, [x19, x1]
ldr     w0, [x19, x0]
mul     w1, w0, w2
STR_MACL w1

//--------------------------------------------------------------
// muls 16bit -> 32 bit Multip
opdesc MULS, (8*4),0,4,0xff,0xff,0xff
opfunc MULS
mov w1, #0 // m
mov w0, #0 // n
ldr     w1, [x19, x1]
ldr     w0, [x19, x0]
sxth	w1, w1
sxth	w0, w0
mul	w1, w1, w0
STR_MACL w1


//--------------------------------------------------------------
// mulu 16bit -> 32 bit Multip
opdesc MULU, (8*4),0,4,0xff,0xff,0xff
opfunc MULU
mov w1, #0 // m
mov w0, #0 // n
ldr     w2, [x19, x1]
ldr     w0, [x19, x0]
uxth    w2, w2
uxth    w0, w0
mul     w1, w0, w2
STR_MACL w1
//CALL_EACHCLOCK

//--------------------------------------------------------------
// MACL   ans = 32bit -> 64 bit MUL
//        (MACH << 32 + MACL)  + ans 
//-------------------------------------------------------------
opdesc MAC_L, ((36)*4),0,4,0xff,0xff,0xff 
opfunc MAC_L
mov w24, #0
mov w23, #0
ldr     w0, [x19, x23]
CALL_GETMEM_LONG
mov     w22, w0
ldr     w0, [x19, x23]
add     w0, w0, 4
str     w0, [x19, x23]
ldr     w0, [x19, x24]
CALL_GETMEM_LONG
smull   x0, w0, w22
ldr     w2, [x19, x24]
ldp     w1, w4, [x19, 76] // LOAD MACH,MACL
add     w2, w2, 4
LDR_SR  w3
str     w2, [x19, x24]
orr     x1, x4, x1, lsl 32
add     x1, x1, x0
tbz     x3, 1, MAC_L.L7
mov     x2, -140737488355328
mov     x3, -281474976710657
add     x2, x1, x2
cmp     x2, x3
bhi     MAC_L.L7
cmp     x0, 0
mov     w2, -32768
mov     w1, 32767
csel    w1, w1, w2, ge
csinv   w0, wzr, wzr, lt
stp     w1, w0, [x19, 76]
b MAC_L.FINISH
MAC_L.L7:
mov     w0, w1
lsr     x1, x1, 32
stp     w1, w0, [x19, 76]
MAC_L.FINISH:

/*
	ldr	w0, [x19,x23]
	CALL_GETMEM_LONG
	mov	w24, w0
	ldr	w0, [x19,x23]
	add	w0, w0, 4
	str	w0, [x19,x23]
	ldr	w0, [x19,x22]
	CALL_GETMEM_LONG
	ldr	w2, [x19,x22]
	add	w2, w2, 4
	smull	x0, w0, w21
	str	w2, [x19,x22]
	orr	x1, x4, x1, lsl 32
	add	x19, x0, x1
	tbz	x3, 1, MAC_L.L6
	mov	x1, -140737488355328
	mov	x2, -281474976710657
	add	x1, x19, x1
	cmp	x1, x2
	bhi	MAC_L.L6
	cmp	x0, xzr
	mov	w1, -32768
	mov	w19, 32767
	csel	w19, w1, w19, lt
	cmp	x0, xzr
	str	w19, [x19,76]
	csinv	w0, wzr, wzr, lt
	str	w0, [x19,80]
	b MAC_L.FINISH
MAC_L.L6:
	mov	w0, w19
	lsr	x19, x19, 32
	STR_MACL	w0
	STR_MACH	w19
MAC_L.FINISH:
*/
/*
mov w0, #0
mov w1, #0
mov w6, w0 
ldr w0, [x19, x1]
mov w22, w1
CALL_GETMEM_LONG
ldr w3, [x19, x22]
add w3, w3, #4
str w3, [x19, x22]
mov w4, #0
mov w5, w0 
ldr w0, [x19, x6]
CALL_GETMEM_LONG
ldr w2, [x19, x6]
add w2, w2, #4
str w2, [x19, x6]
LDR_MACL w1
mov w3,w5
LDR_MACH w5
LDR_SR w2       
orr     w6, w4, w1
smull   w4, w1, w0, w3
adds    w6, w6, w4
adc     ip, w5, w1

tst     w2, #2
beq     MAC_L.NO_S
        mov     w5, #0
        mov     w4, #32768
        adds    w5, w5, w6
        movt    w4, 65535
        adc     w4, w4, ip
        mvn     w2, #65536
        cmp     w4, w2
        mvn     w3, #0
        cmpeq   w5, w3
        bhi     MAC_L.NO_S
        cmp     w0, #0
        sbcs    w2, w1, #0
        mov     w2, #32768
        movw    w1, #32767
        movt    w2, 65535
        mvnge   w4, #0
        movge   w2, w1
        b       MAC_L.FINISH
MAC_L.NO_S:
  mov     w4, w6
  mov     w2, ip
MAC_L.FINISH:
  STR_MACL  w4
  STR_MACH  w2
//CALL_EACHCLOCK
*/

//--------------------------------------------------------------
// MACW   ans = 32bit -> 64 bit MUL
//        (MACH << 32 + MACL)  + ans 
//-------------------------------------------------------------
opdesc MAC_W, (35*4),0,4,0xff,0xff,0xff
opfunc MAC_W
mov    w22, #0
mov    w23, #0
ldr    w0, [x19, x22]
CALL_GETMEM_WORD
sxth    w24, w0
ldr     w0, [x19, x22]
add     w0, w0, 2
str     w0, [x19, x22]
ldr     w0, [x19, x23]
CALL_GETMEM_WORD
sxth    w0, w0
ldr     w2, [x19, x23]
LDR_SR w3
add     w2, w2, 2
ldrsw   x1, [x19, 80]
mul     w0, w24, w0
str     w2, [x19, x23]
add     x1, x1, w0, sxtw
tbz     x3, 1, MAC_W.L10
mov     x2, -2147483648
mov     x3, -4294967297
add     x2, x1, x2
cmp     x2, x3
bhi     MAC_W.L12
LDR_MACH w2
mov     w1, 2147483647
orr     w2, w2, 1
add     w1, w1, w0, lsr 31
STR_MACH w2
MAC_W.L12:
  STR_MACL w1
  b MAC_W.FINISH
MAC_W.L10:
  lsr     x0, x1, 32
  stp     w0, w1, [x19, 76]
MAC_W.FINISH:

//=====================================================
// Leaf function for HWRAM longword reads.
// The generated code calls getmemlong through [x19,#120]; this pointer is set
// to this routine when the fast memory path is enabled (DynarecSh2.cpp).
// x19 = tagSH2 of the running core, hiwram_base at [x19,#168] (see the
// static_assert in DynarecSh2.h), refreshed by SetCurrentContext.
// Only (addr >> 20) == 0x060 is handled here, the case where memGetLong adds
// no cycles (cycles are only added when addr & 0x20000000). Everything else,
// including 0x26 (cache-through), tail-jumps to memGetLong so timing and
// result stay identical. T2ReadLong is a halfword swap (ror #16), not a bswap.
// x11-x13 are free scratch registers (the caller assumes a C call, which
// clobbers x0-x18).
//=====================================================
.section .text
.align 4
.global memGetLongFast
.type memGetLongFast, %function
memGetLongFast:
  ubfx w12, w0, #20, #12
  cmp  w12, #0x060
  b.ne 1f
  ldr  x11, [x19, #168]
  and  w13, w0, #0xFFFFF
  ldr  w0,  [x11, w13, uxtw]
  ror  w0,  w0, #16
  ret
1:
  b    memGetLong
.size memGetLongFast, .-memGetLongFast

// Word and byte reads, same rule as above (only 0x060 directly, otherwise
// tail-jump to the C function; memGetWord/Byte add no cycles for 0x06).
// T2ReadWord is a direct 16-bit load, T2ReadByte is mem[addr ^ 1] (memory.h).
.align 4
.global memGetWordFast
.type memGetWordFast, %function
memGetWordFast:
  ubfx w12, w0, #20, #12
  cmp  w12, #0x060
  b.ne 1f
  ldr  x11, [x19, #168]
  and  w13, w0, #0xFFFFF
  ldrh w0,  [x11, w13, uxtw]
  ret
1:
  b    memGetWord
.size memGetWordFast, .-memGetWordFast

.align 4
.global memGetByteFast
.type memGetByteFast, %function
memGetByteFast:
  ubfx w12, w0, #20, #12
  cmp  w12, #0x060
  b.ne 1f
  ldr  x11, [x19, #168]
  and  w13, w0, #0xFFFFF
  eor  w13, w13, #1
  ldrb w0,  [x11, w13, uxtw]
  ret
1:
  b    memGetByte
.size memGetByteFast, .-memGetByteFast

// HWRAM writes. w0 = addr, w1 = data.
// Presence bit per 16-bit word: idx = (addr & 0xFFFFF) >> 1 (adress_mask),
// array at [x19,#184] (CompileBlocks::parent_bits, u32 words). If the bit
// (for longs: both bits idx and idx+1) is clear, setDirty in C would return
// immediately, so the store is done directly. If it is set, tail-jump to the
// C function (invalidation). Only 0x060 is handled here; 0x26 (cycles) and
// everything else goes to C.
.align 4
.global memSetLongFast
.type memSetLongFast, %function
memSetLongFast:
  ubfx w12, w0, #20, #12
  cmp  w12, #0x060
  b.ne 1f
  ldr  x11, [x19, #184]
  ubfx w13, w0, #1, #19
  lsr  w14, w13, #5
  ldr  w15, [x11, w14, uxtw #2]
  lsr  w15, w15, w13
  tbnz w15, #0, 1f
  add  w13, w13, #1
  lsr  w14, w13, #5
  ldr  w15, [x11, w14, uxtw #2]
  lsr  w15, w15, w13
  tbnz w15, #0, 1f
  ldr  x11, [x19, #168]
  and  w13, w0, #0xFFFFF
  ror  w1,  w1, #16
  str  w1,  [x11, w13, uxtw]
  ret
1:
  b    memSetLong
.size memSetLongFast, .-memSetLongFast

.align 4
.global memSetWordFast
.type memSetWordFast, %function
memSetWordFast:
  ubfx w12, w0, #20, #12
  cmp  w12, #0x060
  b.ne 1f
  ldr  x11, [x19, #184]
  ubfx w13, w0, #1, #19
  lsr  w14, w13, #5
  ldr  w15, [x11, w14, uxtw #2]
  lsr  w15, w15, w13
  tbnz w15, #0, 1f
  ldr  x11, [x19, #168]
  and  w13, w0, #0xFFFFF
  strh w1,  [x11, w13, uxtw]
  ret
1:
  b    memSetWord
.size memSetWordFast, .-memSetWordFast

.align 4
.global memSetByteFast
.type memSetByteFast, %function
memSetByteFast:
  ubfx w12, w0, #20, #12
  cmp  w12, #0x060
  b.ne 1f
  ldr  x11, [x19, #184]
  ubfx w13, w0, #1, #19
  lsr  w14, w13, #5
  ldr  w15, [x11, w14, uxtw #2]
  lsr  w15, w15, w13
  tbnz w15, #0, 1f
  ldr  x11, [x19, #168]
  and  w13, w0, #0xFFFFF
  eor  w13, w13, #1
  strb w1,  [x11, w13, uxtw]
  ret
1:
  b    memSetByte
.size memSetByteFast, .-memSetByteFast

// Extended variants of the read routines above: additionally serve low work
// RAM 0x002xxxxx directly (memGetLong/Word/Byte, case 0x00200000:
// T2Read*(LowWram, addr & 0xFFFFF); cycles are only added with bit 29 set).
// lowram_base is at [x19,#176]. The "5" variants also tail-jump to
// OnchipRead* (addr & 0x1FF) for the on-chip registers 0xFFFFFE00-0xFFFFFFFF,
// the same function used by the fast I/O path in C; it is only assigned when
// that path is enabled (DynarecSh2.cpp).
.section .text
.align 4
.global memGetLongFast4
.type memGetLongFast4, %function
memGetLongFast4:
  ubfx w12, w0, #20, #12
  cmp  w12, #0x060
  b.ne 2f
  ldr  x11, [x19, #168]
3:
  and  w13, w0, #0xFFFFF
  ldr  w0,  [x11, w13, uxtw]
  ror  w0,  w0, #16
  ret
2:
  cmp  w12, #0x002
  b.ne 1f
  ldr  x11, [x19, #176]
  b    3b
1:
  b    memGetLong
.size memGetLongFast4, .-memGetLongFast4

.align 4
.global memGetLongFast5
.type memGetLongFast5, %function
memGetLongFast5:
  ubfx w12, w0, #20, #12
  cmp  w12, #0x060
  b.ne 2f
  ldr  x11, [x19, #168]
3:
  and  w13, w0, #0xFFFFF
  ldr  w0,  [x11, w13, uxtw]
  ror  w0,  w0, #16
  ret
2:
  cmp  w12, #0x002
  b.ne 1f
  ldr  x11, [x19, #176]
  b    3b
1:
  cmn  w0, #0x200
  b.lo 4f
  and  w0, w0, #0x1FF
  b    OnchipReadLong
4:
  b    memGetLong
.size memGetLongFast5, .-memGetLongFast5

.align 4
.global memGetWordFast4
.type memGetWordFast4, %function
memGetWordFast4:
  ubfx w12, w0, #20, #12
  cmp  w12, #0x060
  b.ne 2f
  ldr  x11, [x19, #168]
3:
  and  w13, w0, #0xFFFFF
  ldrh w0,  [x11, w13, uxtw]
  ret
2:
  cmp  w12, #0x002
  b.ne 1f
  ldr  x11, [x19, #176]
  b    3b
1:
  b    memGetWord
.size memGetWordFast4, .-memGetWordFast4

.align 4
.global memGetWordFast5
.type memGetWordFast5, %function
memGetWordFast5:
  ubfx w12, w0, #20, #12
  cmp  w12, #0x060
  b.ne 2f
  ldr  x11, [x19, #168]
3:
  and  w13, w0, #0xFFFFF
  ldrh w0,  [x11, w13, uxtw]
  ret
2:
  cmp  w12, #0x002
  b.ne 1f
  ldr  x11, [x19, #176]
  b    3b
1:
  cmn  w0, #0x200
  b.lo 4f
  and  w0, w0, #0x1FF
  b    OnchipReadWord
4:
  b    memGetWord
.size memGetWordFast5, .-memGetWordFast5

.align 4
.global memGetByteFast4
.type memGetByteFast4, %function
memGetByteFast4:
  ubfx w12, w0, #20, #12
  cmp  w12, #0x060
  b.ne 2f
  ldr  x11, [x19, #168]
3:
  and  w13, w0, #0xFFFFF
  eor  w13, w13, #1
  ldrb w0,  [x11, w13, uxtw]
  ret
2:
  cmp  w12, #0x002
  b.ne 1f
  ldr  x11, [x19, #176]
  b    3b
1:
  b    memGetByte
.size memGetByteFast4, .-memGetByteFast4

.align 4
.global memGetByteFast5
.type memGetByteFast5, %function
memGetByteFast5:
  ubfx w12, w0, #20, #12
  cmp  w12, #0x060
  b.ne 2f
  ldr  x11, [x19, #168]
3:
  and  w13, w0, #0xFFFFF
  eor  w13, w13, #1
  ldrb w0,  [x11, w13, uxtw]
  ret
2:
  cmp  w12, #0x002
  b.ne 1f
  ldr  x11, [x19, #176]
  b    3b
1:
  cmn  w0, #0x200
  b.lo 4f
  and  w0, w0, #0x1FF
  b    OnchipReadByte
4:
  b    memGetByte
.size memGetByteFast5, .-memGetByteFast5

// ===================================================================
// Block chaining.
// epilogue_k / PageFlip_k are the same size as epilogue (10*4) / PageFlip
// (11*4) and replace them when a block is translated. They jump through
// [x19,#216] (chain_target) into chain_stub, because the block code lives in an
// mmap area and a direct b would be out of range.
// Register state at block exit: x19 tagSH2, w20 PC, w21 COUNT, w25 exitcount
// (= targetcnt of the time slice), w26 SR; 80-byte stack frame from the prolog.
// Offsets: SR 64, PC 88, COUNT 92, ICOUNT 96, memcycle 192, lookup_hw 200,
// cur_block 208, chain_target 216 (static_assert in DynarecSh2.h),
// Block.flags 2060, PROLOGSIZE 44.
// ===================================================================
.section .text
.align 4
.global epilogue_k
.type epilogue_k, %function
epilogue_k:
  ldr  x9, [x19, #216]
  br   x9
  nop
  nop
  nop
  nop
  nop
  nop
  nop
  nop
.size epilogue_k, .-epilogue_k

.align 4
.global PageFlip_k
.type PageFlip_k, %function
PageFlip_k:
  cbz  w0, 1f
  mov  w20, w0
  ldr  x9, [x19, #216]
  br   x9
  nop
  nop
  nop
  nop
  nop
  nop
  nop
1:
.size PageFlip_k, .-PageFlip_k

// Delayed branch exits through chain_stub.
// Same size as seperator_delay_slot (15*4) / seperator_delay_after (12*4).
// Register state as for epilogue_k/PageFlip_k (x19 tagSH2, w20 PC, w21 COUNT,
// w25 exitcount, w26 SR, 80-byte frame); chain_stub itself stores PC/COUNT/SR
// (hit and fallback path). The cycle slot of _after stays at offset 8.
.align 4
.global seperator_delay_slot_k
.type seperator_delay_slot_k, %function
seperator_delay_slot_k:
  cbnz w0, 1f              // 0   branch taken -> 1: (as continue:)
  add  w20, w20, #2        // 4   not taken: PC = delay slot = next block
  add  w21, w21, #1        // 8   as original (unpatched)
  ldr  x9, [x19, #216]     // 12
  br   x9                  // 16  -> chain_stub
  nop                      // 20
  nop                      // 24
  nop                      // 28
  nop                      // 32
  nop                      // 36
  nop                      // 40
  nop                      // 44
  nop                      // 48
1:
  mov  w20, w0             // 52  branch target
  sub  w20, w20, #2        // 56  PC -= 2, continue into the delay-slot instruction
.size seperator_delay_slot_k, .-seperator_delay_slot_k
.if (. - seperator_delay_slot_k) != 60
.error "seperator_delay_slot_k must be 15*4 bytes"
.endif

.align 4
.global seperator_delay_after_k
.type seperator_delay_after_k, %function
seperator_delay_after_k:
  add  w20, w20, #2        // 0   PC = branch target
  nop                      // 4   (STR_PC is done by chain_stub)
  add  w21, w21, #1        // 8   cycle slot (EmmitCode patches the cycle count in here)
  ldr  x9, [x19, #216]     // 12
  br   x9                  // 16  -> chain_stub
  nop                      // 20
  nop                      // 24
  nop                      // 28
  nop                      // 32
  nop                      // 36
  nop                      // 40
  nop                      // 44
.size seperator_delay_after_k, .-seperator_delay_after_k
.if (. - seperator_delay_after_k) != 48
.error "seperator_delay_after_k must be 12*4 bytes"
.endif

.align 4
.global chain_stub
.type chain_stub, %function
chain_stub:
  ldr  x9,  [x19, #208]          // cur_block
  ldr  w10, [x9, #2060]          // flags
  tbnz w10, #0, 9f               // BLOCK_LOOP -> C (idle-skip branch)
  ldr  w11, [x19, #96]           // ICOUNT = SysReg[5]
  and  w12, w26, #0xF0
  cmp  w12, w11
  b.lo 9f                        // (SR&0xF0) < ICOUNT -> C accepts the interrupt
  ldr  w13, [x19, #192]          // memcycle
  add  w14, w21, w13
  cmp  w14, w25
  b.hs 9f                        // SysReg[4] >= targetcnt -> time slice over (loop condition)
  ubfx w15, w20, #20, #8
  cmp  w15, #0x60
  b.ne 9f                        // (PC & 0x0FF00000) != 0x06000000 -> C (range chain)
  ldr  x16, [x19, #200]          // lookup_hw
  ubfx w17, w20, #1, #19         // (PC & 0xFFFFF) >> 1
  ldr  x9,  [x16, w17, uxtw #3]
  cbz  x9, 9f                    // not translated -> C (CompileBlock)
  mov  w21, w14                  // fold memcycle (as SysReg[4] += memcycle)
  str  wzr, [x19, #192]
  str  x9,  [x19, #208]          // cur_block = next block
  STR_PC w20                     // stored state as after the old epilogue + prologue
  STR_COUNT w21
  STR_SR_REG w26
  add  x9, x9, #44               // behind the prologue (the frame stays)
  br   x9
9:                               // = old epilogue
  STR_PC w20
  STR_COUNT w21
  STR_SR_REG w26
  ldp  x19, x20, [sp]
  ldp  x21, x22, [sp,#16]
  ldp  x23, x24, [sp,#32]
  ldp  x29, x30, [sp,#48]
  ldp  x25, x26, [sp,#64]
  add  sp, sp, #80
  ret
.size chain_stub, .-chain_stub

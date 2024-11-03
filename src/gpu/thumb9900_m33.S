/*
 * Project: pico9918
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 * 
 * Purpose: TMS9900 GPU core (RP2350)
 * 
 * Author: JasonACT (AtariAge)
 *
 */

.syntax unified // ARM unified syntax
.thumb_func     // Code uses Thumb instructions

.section .data  // In RAM Please

.global run9900 // Entry point

//.extern F18A_PIX

// *********************************************************************************************
//.extern debug_print
//        BL   sav9900
//        BLX  debug_print
//        BL   res9900
// *********************************************************************************************
/*
sav9900: // Save simulator state and restore GCC registers except R0-R3
        PUSH {R0-R7} // 8 ... PUSHes in reverse: R7..R0
        MOV  R4,R8
        MOV  R5,R9
        MOV  R6,R10
        MOV  R7,R11
        PUSH {R4-R7} // +4 = 12 ... R11..R8
        MOV  PC,LR
// *********************************************************************************************
res9900: // Restore simulator state
        POP  {R0-R3}
        MOV  R8,R0
        MOV  R9,R1
        MOV  R10,R2
        MOV  R11,R3
        POP  {R0-R7} // POPs in order: R0..R7
        MOV  PC,LR
*/
// *********************************************************************************************

// Instructions

// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567 (Format 5)
//CCCCWWWW00001000
.align 4
I_SRA:  LSLS R5,R0,#20
        LSRS R5,#27 // Get Register x 2
        MOV  R6,R11

        LSRS R2,R0,#12 // Get Shift Count
        BNE  I_SRA_nr0 // Result not 0 - got it, else
        LDRB R2,[R6,#1] // Get R0 low byte
        ANDS R2,#0x0F  // Mask lower nibble
        BNE  I_SRA_nr0 // Result not 0 - got it, else
        MOVS R2,#16
I_SRA_nr0:
        LDRH R0,[R5,R6]
        REVSH R0,R0 // Signed

        ANDS R1,#0x0E // Get initialised ST
        
        ASRS R0,R2
        BCC  I_SRA_st // Carry Clear?
        ADDS R1,#0x10 // Carry Bit
I_SRA_st:
        REV16 R2,R0
        STRH R2,[R5,R6]
        B    OP_COMP_W
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567 (Format 5)
//CCCCWWWW00001001
.align 4
I_SRL:  LSLS R5,R0,#20
        LSRS R5,#27 // Get Register x 2
        MOV  R6,R11

        LSRS R2,R0,#12 // Get Shift Count
        BNE  I_SRL_nr0 // Result not 0 - got it, else
        LDRB R2,[R6,#1] // Get R0 low byte
        ANDS R2,#0x0F  // Mask lower nibble
        BNE  I_SRL_nr0 // Result not 0 - got it, else
        MOVS R2,#16
I_SRL_nr0:
        LDRH R0,[R5,R6]
        REV16 R0,R0 // Unsigned

        ANDS R1,#0x0E // Get initialised ST
        
        LSRS R0,R2
        BCC  I_SRL_st // Carry Clear?
        ADDS R1,#0x10 // Carry Bit
I_SRL_st:
        REV16 R2,R0
        STRH R2,[R5,R6]
        B    OP_COMP_W
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567 (Format 5)
//CCCCWWWW00001010
.align 4
I_SLA:  LSLS R5,R0,#20
        LSRS R5,#27 // Get Register x 2
        MOV  R6,R11

        ANDS R1,#0x06 // Get initialised ST

        LSRS R2,R0,#12 // Get Shift Count
        BNE  I_SLA_nr0 // Result not 0 - got it, else
        LDRB R2,[R6,#1] // Get R0 low byte
        ANDS R2,#0x0F  // Mask lower nibble
        BNE  I_SLA_nr0 // Result not 0 - got it, else
        MOVS R2,#16

        LDRH R0,[R5,R6]
        REV16 R0,R0 // Unsigned
        LSLS R4,R0,#16
        ORRS R0,R4 // Copy lower halfword to high halfword

        // Set Overflow if sign changes
        BNE  I_SLA_ov // Result was not zero
        B    I_SLA_nov
I_SLA_nr0:
        LDRH R0,[R5,R6]
        REV16 R0,R0 // Unsigned
        LSLS R4,R0,#16
        ORRS R0,R4 // Copy lower halfword to high halfword

        // Set Overflow if sign changes
        ADR  R4,I_SLA_M
        LSLS R7,R2,#2
        LDR  R7,[R4,R7] // Get correct mask

        LSRS R4,R0,#16 // Top bit into Carry
        BCS  I_SLA_neg // Result was not zero

        // Positive number test
        TST  R0,R7
        BNE  I_SLA_ov // Result was not zero
        B    I_SLA_nov
I_SLA_neg:
        // Negative number test
        MOV  R4,R0
        ANDS R4,R7
        CMP  R4,R7
        BEQ  I_SLA_nov // Sign bit never changes
I_SLA_ov:
        ADDS R1,#0x08 // Overflow Bit
I_SLA_nov:
        LSLS R0,R2
        BCC  I_SLA_st // Carry Clear?
        ADDS R1,#0x10 // Carry Bit
I_SLA_st:
        REV16 R2,R0
        STRH R2,[R5,R6]
        B    OP_COMP_W
.align 4
I_SLA_M: .WORD 0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00
         .WORD 0x7F80, 0x7FC0, 0x7FE0, 0x7FF0, 0x7FF8, 0x7FFC, 0x7FFE, 0x7FFF
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567 (Format 5)
//CCCCWWWW00001011
.align 4
I_SRC:  LSLS R5,R0,#20
        LSRS R5,#27 // Get Register x 2
        MOV  R6,R11

        LSRS R2,R0,#12 // Get Shift Count
        BNE  I_SRC_nr0 // Result not 0 - got it, else
        LDRB R2,[R6,#1] // Get R0 low byte
        ANDS R2,#0x0F  // Mask lower nibble
        BNE  I_SRC_nr0 // Result not 0 - got it, else
        MOVS R2,#16
I_SRC_nr0:
        LDRH R0,[R5,R6]
        REV16 R0,R0 // Unsigned
        LSLS R4,R0,#16
        ORRS R0,R4 // Copy lower halfword to high halfword

        ANDS R1,#0x0E // Get initialised ST
        
        RORS R0,R2
        BCC  I_SRC_st // Carry Clear?
        ADDS R1,#0x10 // Carry Bit
I_SRC_st:
        REV16 R2,R0
        STRH R2,[R5,R6]
        B    OP_COMP_W
// *********************************************************************************************

// F18A Instructions (part I)

// *********************************************************************************************
.align 4
I_SLC:
//R0 (LE)
//89ABCDEF01234567 (Format 5)
//CCCCWWWW00001110
I_SLC:  LSLS R5,R0,#20
        LSRS R5,#27 // Get Register x 2
        MOV  R6,R11

        LSRS R2,R0,#12 // Get Shift Count
        BNE  I_SLC_nr0 // Result not 0 - got it, else
        LDRB R2,[R6,#1] // Get R0 low byte
        ANDS R2,#0x0F  // Mask lower nibble
        BNE  I_SLC_nr0 // Result not 0 - got it, else
        MOVS R2,#16
I_SLC_nr0:
        LDRH R0,[R5,R6]
        REV16 R0,R0 // Unsigned
        LSLS R4,R0,#16
        ORRS R0,R4 // Copy lower halfword to high halfword

        ANDS R1,#0x0E // Get initialised ST

        MOVS R4,#16 // Simulate Rotate Left
        SUBS R4,R2  // R2 = 1..16, R4 -> 15..0
        RORS R0,R4
        LSRS R2,R0,#1 // Calculate Carry
        BCC  I_SLC_st // Carry Clear?
        ADDS R1,#0x10 // Carry Bit
I_SLC_st:
        REV16 R2,R0
        STRH R2,[R5,R6]
        B    OP_COMP_W

// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//ooo.WWWW00000010
.align 4
I_LI:   LSLS R6,R0,#20
        LSRS R6,#27 // DEST = WP + ((BYTE & 0x0F) << 1);
        LDRH R0,[R8,R3]
        ADDS R3,#2
        STRH R0,[R11,R6]
        REV16 R0,R0
        ANDS R1,#0x1E // Get initialised ST
        B    OP_COMP_W
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//ooo.WWWW00000010
.align 4
I_ANDI: LSLS R6,R0,#20
        LSRS R6,#27 // DEST = WP + ((BYTE & 0x0F) << 1);
        LDRH R0,[R8,R3]
        ADDS R3,#2
        LDRH R5,[R11,R6] // R6=DEST
        ANDS R0,R5
        STRH R0,[R11,R6] // R6=DEST
        REV16 R0,R0
        ANDS R1,#0x1E // Get initialised ST
        B    OP_COMP_W
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//ooo.WWWW00000010
.align 4
I_ORI:  LSLS R6,R0,#20
        LSRS R6,#27 // DEST = WP + ((BYTE & 0x0F) << 1);
        LDRH R0,[R8,R3]
        ADDS R3,#2
        LDRH R5,[R11,R6] // R6=DEST
        ORRS R0,R5
        STRH R0,[R11,R6] // R6=DEST
        REV16 R0,R0
        ANDS R1,#0x1E // Get initialised ST
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_NEG:  LDRH R0,[R5,#0] // R5=SRCE/DEST
        REV16 R0,R0

        ANDS R1,#0x06 // Get initialised ST

        MOVS R4,#0x80
        LSLS R4,#8 // -> #0x8000
        CMP  R0,R4
        BNE  I_NEG_no_ov
        ADDS R1,#0x08 // Overflow bit
        B    OP_COMP_W
.align 4
I_NEG_no_ov:
        RSBS R0,#0 // R0=0-R0
        BNE  I_NEG_nc
        ADDS R1,#0x10 // Carry bit
I_NEG_nc:
        REV16 R2,R0
        STRH R2,[R5,#0]
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_INV:  LDRH R0,[R5,#0] // R5=SRCE/DEST
        MVNS R0,R0
        STRH R0,[R5,#0]
        REV16 R0,R0
        ANDS R1,#0x1E // Get initialised ST
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_XOR:  EORS R4,R5 // R4=TEMP, R5=*SRCE
        REV16 R4,R4
        STRH R4,[R6,#0] // R6=DEST
        REV16 R0,R4
        ANDS R1,#0x1E // Get initialised ST
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_SZC:  LDRH R0,[R6,#0] // R6=DEST
        BICS R0, R5
        STRH R0,[R6,#0]
        REV16 R0,R0
        ANDS R1,#0x1E // Get initialised ST
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_SOC:  LDRH R0,[R6,#0]
        ORRS R0,R5
        STRH R0,[R6,#0]
        REV16 R0,R0
        ANDS R1,#0x1E // Get initialised ST
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_ABS:  LDRH R0,[R5,#0] // R5=SRCE
        REV16 R0,R0

        ANDS R1,#0x06 // Get initialised ST

        MOVS R4,#0x80
        LSLS R4,#8 // -> #0x8000
        TST  R0,R4 // pos check...
        BEQ  OP_COMP_W
        CMP  R0,R4
        BNE  I_ABS_no_ov
        ADDS R1,#0x08 // Overflow bit
        B    OP_COMP_W
.align 4
I_ABS_no_ov:
        RSBS R2,R0,#0 // R2=0-R0
        REV16 R2,R2
        STRH R2,[R5,#0]
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_INC:  LDRH R2,[R5,#0] // R5=SRCE/DEST
        REV16 R2,R2

        ADDS R4,R2,#1

        UXTH R0,R4 // Keep a clean copy
        REV16 R4,R4
        STRH R4,[R5,#0]

        ANDS R1,#0x06 // Get initialised ST

        CMP  R2,R0
        BLO  I_INC_nc
        ADDS R1,#0x10 // Carry bit
I_INC_nc:
        LSRS R2,#15
        BNE  OP_COMP_W
        LSRS R4,R0,#15
        CMP  R2,R4
        BEQ  OP_COMP_W
        ADDS R1,#0x08 // Overflow bit
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_INCT: LDRH R2,[R5,#0] // R5=SRCE/DEST
        REV16 R2,R2

        ADDS R4,R2,#2

        UXTH R0,R4 // Keep a clean copy
        REV16 R4,R4
        STRH R4,[R5,#0]

        ANDS R1,#0x06 // Get initialised ST

        CMP  R2,R0
        BLO  I_INCT_nc
        ADDS R1,#0x10 // Carry bit
I_INCT_nc:
        LSRS R2,#15
        BNE  OP_COMP_W
        LSRS R4,R0,#15
        CMP  R2,R4
        BEQ  OP_COMP_W
        ADDS R1,#0x08 // Overflow bit
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_DEC:  LDRH R2,[R5,#0] // R5=SRCE/DEST
        REV16 R2,R2
        SUBS R4,R2,#1

        UXTH R0,R4 // Keep a clean copy
        REV16 R4,R4
        STRH R4,[R5,#0]

        ANDS R1,#0x06 // Get initialised ST

        CMP  R2,R0
        BLO  I_DEC_nc
        ADDS R1,#0x10 // Carry bit
I_DEC_nc:
        LSRS R2,#15
        BEQ  OP_COMP_W
        LSRS R4,R0,#15
        CMP  R2,R4
        BEQ  OP_COMP_W
        ADDS R1,#0x08 // Overflow bit
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_DECT: LDRH R2,[R5,#0] // R5=SRCE/DEST
        REV16 R2,R2

        SUBS R4,R2,#2

        UXTH R0,R4 // Keep a copy
        REV16 R4,R4
        STRH R4,[R5,#0]

        ANDS R1,#0x06 // Get initialised ST

        CMP  R2,R0
        BLO  I_DECT_nc
        ADDS R1,#0x10 // Carry bit
I_DECT_nc:
        LSRS R2,#15
        BEQ  OP_COMP_W
        LSRS R4,R0,#15
        CMP  R2,R4
        BEQ  OP_COMP_W
        ADDS R1,#0x08 // Overflow bit
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_MOV:  STRH R5,[R6,#0]
        REV16 R0,R5
        ANDS R1,#0x1E // Get initialised ST
// *********************************************************************************************
OP_COMP_W:
        SXTH R0,R0
        CMP  R0,#0
        BNE  OP_COMP_W_l
        ADDS R1,#0x20 // Equal bit
        B    start
.align 4
OP_COMP_W_l:
        BGT  OP_COMP_W_a
        ADDS R1,#0x80 // L >
        B    start
.align 4
OP_COMP_W_a:
        ADDS R1,#0xC0 // LA >
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//ooo.WWWW00000010
.align 4
I_AI:   LSLS R6,R0,#20
        LSRS R6,#27
        ADD  R6,R11 // DEST = WP + ((BYTE & 0x0F) << 1);
        LDRH R5,[R8,R3]
        ADDS R3,#2
// *********************************************************************************************
I_A:    LDRH R2,[R6,#0] // R6=DEST
        REV16 R2,R2
        REV16 R5,R5

        ADDS R7,R2,R5

        ANDS R1,#0x06 // Get initialised ST

        LSRS R4,R7,#16
        BEQ  I_A_nc
        ADDS R1,#0x10 // Carry bit
I_A_nc:
        UXTH R0,R7 // Keep a copy
        REV16 R7,R7
        STRH R7,[R6,#0]

        LSRS R5,#15
        LSRS R2,#15
        CMP  R2,R5
        BNE  OP_COMP_W
        LSRS R4,R0,#15
        CMP  R2,R4
        BEQ  OP_COMP_W
        ADDS R1,#0x08 // Overflow bit
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_S:    LDRH R2,[R6,#0] // R6=DEST
        REV16 R2,R2
        REV16 R5,R5

        SUBS R4,R2,R5

        UXTH R0,R4 // Keep a copy
        REV16 R4,R4
        STRH R4,[R6,#0]

        ANDS R1,#0x06 // Get initialised ST

        CBZ  R5,I_S_c // Zero sets Carry
        CMP  R2,R0
        BLO  I_S_nc
I_S_c:
        ADDS R1,#0x10 // Carry bit
I_S_nc:
        LSRS R5,#15
        LSRS R2,#15
        CMP  R2,R5
        BEQ  OP_COMP_W
        LSRS R4,R0,#15
        CMP  R2,R4
        BEQ  OP_COMP_W
        ADDS R1,#0x08 // Overflow bit
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_SZCB: LDRB R0,[R6,#0] // R6=DEST
        BICS R0, R5
        STRB R0,[R6,#0]
        ANDS R1,#0x1A // Get initialised ST
        B    OP_COMP_B
// *********************************************************************************************
.align 4
I_SOCB: LDRB R0,[R6,#0]
        ORRS R0,R5
        STRB R0,[R6,#0]
        ANDS R1,#0x1A // Get initialised ST
        B    OP_COMP_B
// *********************************************************************************************
.align 4
I_AB:   LDRB R2,[R6,#0] // R6=DEST

        ADDS R0,R2,R5

        ANDS R1,#0x02 // Get initialised ST

        LSRS R4,R0,#8
        BEQ  I_AB_nc
        ADDS R1,#0x10 // Carry bit
        UXTB R0,R0
I_AB_nc:
        STRB R0,[R6,#0]

        LSRS R5,#7
        LSRS R2,#7
        CMP  R2,R5
        BNE  OP_COMP_B
        LSRS R4,R0,#7
        CMP  R2,R4
        BEQ  OP_COMP_B
        ADDS R1,#0x08 // Overflow bit
        B    OP_COMP_B
// *********************************************************************************************
.align 4
I_SB:   LDRB R2,[R6,#0] // R6=DEST

        SUBS R4,R2,R5

        UXTB R0,R4 // Keep a copy
        STRB R4,[R6,#0]

        ANDS R1,#0x02 // Get initialised ST

        CBZ  R5,I_SB_c // Zero sets Carry
        CMP  R2,R0
        BLO  I_SB_nc
I_SB_c:
        ADDS R1,#0x10 // Carry bit
I_SB_nc:
        LSRS R5,#7
        LSRS R2,#7
        CMP  R2,R5
        BEQ  OP_COMP_B
        LSRS R4,R0,#7
        CMP  R2,R4
        BEQ  OP_COMP_B
        ADDS R1,#0x08 // Overflow bit
        B    OP_COMP_B
// *********************************************************************************************
.align 4
I_MOVB: STRB R5,[R6,#0]
        MOVS R0,R5
        ANDS R1,#0x1A // Get initialised ST
// *********************************************************************************************
OP_COMP_B:
        ADR  R4,PARITY0
        LDRB R4,[R4,R0]
        ORRS R1,R4
        B    start
// *********************************************************************************************
.align 4
I_C:    LDRH R2,[R6,#0] // R6=DEST
        REV16 R2,R2
        REV16 R5,R5

        ANDS R1,#0x1E // Get initialised ST

        CMP  R5,R2
        BNE  I_C_l
        ADDS R1,#0x20 // Equal bit
        B    start
.align 4
I_C_l:
        BLO  I_C_a
        ADDS R1,#0x80 // Logic > bit
I_C_a:
        SXTH R5,R5
        SXTH R2,R2
        CMP  R5,R2 // Redo flags
        BLT  I_C_e
        ADDS R1,#0x40 // Arith > bit
I_C_e:
        B    start
// *********************************************************************************************
.align 4
I_CB:   LDRB R2,[R6,#0] // R6=DEST

        ANDS R1,#0x1A // Get initialised ST

        ADR  R4,PARITY
        LDRB R4,[R4,R5]
        ORRS R1,R4

        CMP  R5,R2
        BNE  I_CB_l
        ADDS R1,#0x20 // Equal bit
        B    start
I_CB_l:
.align 4
        BLO  I_CB_a
        ADDS R1,#0x80 // Logic > bit
I_CB_a:
        SXTB R5,R5
        SXTB R2,R2
        CMP  R5,R2 // Redo flags
        BLT  I_CB_e
        ADDS R1,#0x40 // Arith > bit
I_CB_e:
        B    start
// *********************************************************************************************
.align 4
PARITY:
    .BYTE 0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0
    .BYTE 4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4
    .BYTE 4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4
    .BYTE 0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0
    .BYTE 4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4
    .BYTE 0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0
    .BYTE 0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0
    .BYTE 4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4
    .BYTE 4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4
    .BYTE 0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0
    .BYTE 0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0
    .BYTE 4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4
    .BYTE 0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0
    .BYTE 4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4
    .BYTE 4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4
    .BYTE 0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0
PARITY0: // Compare to 0 status result
    .BYTE 0x20, 0xC4, 0xC4, 0xC0, 0xC4, 0xC0, 0xC0, 0xC4, 0xC4, 0xC0, 0xC0, 0xC4, 0xC0, 0xC4, 0xC4, 0xC0
    .BYTE 0xC4, 0xC0, 0xC0, 0xC4, 0xC0, 0xC4, 0xC4, 0xC0, 0xC0, 0xC4, 0xC4, 0xC0, 0xC4, 0xC0, 0xC0, 0xC4
    .BYTE 0xC4, 0xC0, 0xC0, 0xC4, 0xC0, 0xC4, 0xC4, 0xC0, 0xC0, 0xC4, 0xC4, 0xC0, 0xC4, 0xC0, 0xC0, 0xC4
    .BYTE 0xC0, 0xC4, 0xC4, 0xC0, 0xC4, 0xC0, 0xC0, 0xC4, 0xC4, 0xC0, 0xC0, 0xC4, 0xC0, 0xC4, 0xC4, 0xC0
    .BYTE 0xC4, 0xC0, 0xC0, 0xC4, 0xC0, 0xC4, 0xC4, 0xC0, 0xC0, 0xC4, 0xC4, 0xC0, 0xC4, 0xC0, 0xC0, 0xC4
    .BYTE 0xC0, 0xC4, 0xC4, 0xC0, 0xC4, 0xC0, 0xC0, 0xC4, 0xC4, 0xC0, 0xC0, 0xC4, 0xC0, 0xC4, 0xC4, 0xC0
    .BYTE 0xC0, 0xC4, 0xC4, 0xC0, 0xC4, 0xC0, 0xC0, 0xC4, 0xC4, 0xC0, 0xC0, 0xC4, 0xC0, 0xC4, 0xC4, 0xC0
    .BYTE 0xC4, 0xC0, 0xC0, 0xC4, 0xC0, 0xC4, 0xC4, 0xC0, 0xC0, 0xC4, 0xC4, 0xC0, 0xC4, 0xC0, 0xC0, 0xC4
    .BYTE 0x84, 0x80, 0x80, 0x84, 0x80, 0x84, 0x84, 0x80, 0x80, 0x84, 0x84, 0x80, 0x84, 0x80, 0x80, 0x84
    .BYTE 0x80, 0x84, 0x84, 0x80, 0x84, 0x80, 0x80, 0x84, 0x84, 0x80, 0x80, 0x84, 0x80, 0x84, 0x84, 0x80
    .BYTE 0x80, 0x84, 0x84, 0x80, 0x84, 0x80, 0x80, 0x84, 0x84, 0x80, 0x80, 0x84, 0x80, 0x84, 0x84, 0x80
    .BYTE 0x84, 0x80, 0x80, 0x84, 0x80, 0x84, 0x84, 0x80, 0x80, 0x84, 0x84, 0x80, 0x84, 0x80, 0x80, 0x84
    .BYTE 0x80, 0x84, 0x84, 0x80, 0x84, 0x80, 0x80, 0x84, 0x84, 0x80, 0x80, 0x84, 0x80, 0x84, 0x84, 0x80
    .BYTE 0x84, 0x80, 0x80, 0x84, 0x80, 0x84, 0x84, 0x80, 0x80, 0x84, 0x84, 0x80, 0x84, 0x80, 0x80, 0x84
    .BYTE 0x84, 0x80, 0x80, 0x84, 0x80, 0x84, 0x84, 0x80, 0x80, 0x84, 0x84, 0x80, 0x84, 0x80, 0x80, 0x84
    .BYTE 0x80, 0x84, 0x84, 0x80, 0x84, 0x80, 0x80, 0x84, 0x84, 0x80, 0x80, 0x84, 0x80, 0x84, 0x84, 0x80
// *********************************************************************************************
.align 4
I_COC:  ANDS R4,R5
        CMP  R4,R5
        BNE  I_COC_ne
        ORRS R1,#0x20 // EQ
        B    start
.align 4
I_COC_ne:
        ANDS R1,#0xDF // ~EQ
        B    start
// *********************************************************************************************
.align 4
I_CZC:  ANDS R4,R5
        BNE  I_CZC_ne
        ORRS R1,#0x20 // EQ
        B    start
.align 4
I_CZC_ne:
        ANDS R1,#0xDF // ~EQ
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//ooo.WWWW00000010
.align 4
I_CI:   LSLS R6,R0,#20
        LSRS R6,#27 // DEST = WP + ((BYTE & 0x0F) << 1);
        LDRH R5,[R8,R3]
        ADDS R3,#2
        REV16 R5,R5

        LDRH R2,[R11,R6] // R6=DEST
        REV16 R2,R2

        ANDS R1,#0x1E // Get initialised ST

        CMP  R2,R5
        BNE  I_CI_l
        ADDS R1,#0x20 // Equal bit
        B    start
.align 4
I_CI_l:
        BLO  I_CI_a
        ADDS R1,#0x80 // Logic > bit
I_CI_a:
        SXTH R5,R5
        SXTH R2,R2
        CMP  R2,R5 // Redo flags
        BLT  I_CI_e
        ADDS R1,#0x40 // Arith > bit
I_CI_e:
        B    start
// *********************************************************************************************
.align 4
I_MUL:  MULS R4,R5
        REV16 R4,R4
        STRH R4,[R6,#2]
        LSRS R4,16
        STRH R4,[R6,#0]
        B    start
// *********************************************************************************************
.align 4
I_DIV:  CMP   R5,R4
        BLS   I_DIV_ov // Equal also overflows

        LSLS R4,16
        LDRH R2,[R6,#2]
        REV16 R2,R2
        ORRS R4,R2

        UDIV R2,R4,R5    // Quotient in R2
        MLS  R5,R2,R5,R4 // Remainder - R5=R4-(R2*R5)
        ANDS R1,#0xF7 // ~OV

        REV16 R2,R2
        STRH R2,[R6,#0]

        REV16 R5,R5
        STRH R5,[R6,#2]
        B    start
.align 4
I_DIV_ov:
        ORRS R1,#0x08 // OV
        B    start
// *********************************************************************************************
.align 4
I_LWPI: LDRH R0,[R8,R3]
        ADDS R3,#2
        REV16 R0,R0
        BICS R0,#1 // R0 &= 0xFFFE
        ADD  R11,R0,R8
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//ooo.WWWW00000010
.align 4
I_STWP: LSLS R6,R0,#20
        LSRS R6,#27 // DEST = WP + ((BYTE & 0x0F) << 1);
        SUB  R4,R11,R8
        REV16 R4,R4
        STRH R4,[R6,R11]
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//ooo.WWWW00000010
.align 4
I_STST: LSLS R6,R0,#20
        LSRS R6,#27 // DEST = WP + ((BYTE & 0x0F) << 1);
        STRH R1,[R11,R6] // Don't need to swap low bytes here
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//ooo.WWWW00000011
.align 4
I_LIMI: ADDS R3,#2    // Skip LIMI's 2nd WORD
        B    start
// *********************************************************************************************
.align 4
I_BLWP: LDRH R0,[R5,#0] // R5=SRCE
        REV16 R0,R0
        BICS R0,#1 // R0 &= 0xFFFE
        MOV  R7,R8
        ADDS R0,R7
        MOV  R2,R11 // WP
        SUBS R2,R7
        MOV  R11,R0

        REV16 R2,R2
        STRH R2,[R0,#26] // R13

        REV16 R3,R3
        STRH R3,[R0,#28] // R14

        STRH R1,[R0,#30] // R15 High Byte = ST

        LDRH R3,[R5,#2]
        REV16 R3,R3
        BICS R3,#1 // R3 &= 0xFFFE
        B    start
// *********************************************************************************************
.align 4
I_RTWP: MOV  R5,R11  // Get address of WP

        LDRB R1,[R5,#30] // R15 high byte

        LDRH R3,[R5,#28] // R14..
        REV16 R3,R3
        BICS R3,#1 // R3 &= 0xFFFE

        LDRH R0,[R5,#26] // R13..
        REV16 R0,R0
        BICS R0,#1 // R0 &= 0xFFFE
        ADD  R11,R0,R8
        B    start
// *********************************************************************************************
.align 4
I_B:    SUBS R3,R5,R8 // Normalise
        B    start
// *********************************************************************************************
.align 4
I_BL:   REV16 R3,R3
        STRH R3,[R11,#22]
        SUBS R3,R5,R8 // Normalise
        B    start
// *********************************************************************************************
.align 4
I_X:    LDRH R0,[R5,#0] // R5=SRCE
        B    startX
// *********************************************************************************************
.align 4
I_SWPB: LDRH R0,[R5,#0] // R5=SRCE
        REV16 R0,R0
        STRH R0,[R5,#0]
        B    start
// *********************************************************************************************
.align 4
I_SETO: MOVS R0,0xFF
        SXTB R0,R0
        STRH R0,[R5,#0]
        B    start
// *********************************************************************************************
.align 4
I_CLR:  MOVS R0,#0
        STRH R0,[R5,#0]
        B    start
// *********************************************************************************************
.align 4
I_SBO: // SBO - Not Implemented
I_SBZ: // SBZ - Not Implemented
        B    start
.align 4
I_TB:  // TB  - Not Implemented
        ANDS R1,#0xDF // ~EQ
        B    start

// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//ooTsSSSS000000oo
//R6 (LE)
//0123456789ABCDEF
//000000ooooTsSSSS
// *********************************************************************************************
.align 4
sdecode2a: // calculate S & SRCE
        LSLS R5,R6,#28
        LSRS R5,#27 // Got S << 1
        LSRS R2,R0,#12 // Got TS, but with 2 extra random hi bits
        ADR  R4,COM_S2a
        LDR  PC,[R4,R2,LSL #2]
.align 4
COM_S2a: .WORD COM_S2a_R+1, COM_S2a_I+1, COM_S2a_L+1, COM_S2a_II+1 // random bits 00
         .WORD COM_S2a_R+1, COM_S2a_I+1, COM_S2a_L+1, COM_S2a_II+1 // random bits 01
         .WORD COM_S2a_R+1, COM_S2a_I+1, COM_S2a_L+1, COM_S2a_II+1 // random bits 10
         .WORD COM_S2a_R+1, COM_S2a_I+1, COM_S2a_L+1, COM_S2a_II+1 // random bits 11
.align 4
COM_S2a_R:
        ADD  R5,R11 // SRCE = WP + S;
        MOV  PC,R7
.align 4
COM_S2a_L:
        LDRH R2,[R8,R3] // SRCE = (memory [PC + 0] << 8) | memory [PC + 1];
        ADDS R3,#2
        REV16 R2,R2
        CBZ  R5,COM_S2a_F  // if (S) // Not R0?
        LDRH R4,[R11,R5] // SRCE += (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R4,R4
        ADD  R2,R4
        UXTH R2,R2
COM_S2a_F:
        BICS R5,R2,#1 // SRCE &= 0xFFFE
        ADD  R5,R8
        MOV  PC,R7
.align 4
COM_S2a_II:
        LDRH R2,[R11,R5] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R2,R2
        ADDS R4,R2,#2
        REV16 R4,R4
        STRH R4,[R11,R5]
        BICS R5,R2,#1 // SRCE &= 0xFFFE
        ADD  R5,R8
        MOV  PC,R7
.align 4
COM_S2a_I:
        LDRH R5,[R11,R5] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R5,R5
        BICS R5,#1 // SRCE &= 0xFFFE
        ADD  R5,R8
        MOV  PC,R7
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567 (Format 3)
//DDTsSSSS0010ooDD
//R6 (BE)
//0123456789ABCDEF
//0010ooDDDDTsSSSS
// *********************************************************************************************
.align 4
sdecode2b: // calculate S & SRCE
        LSLS R5,R6,#28
        LSRS R5,#27 // Got S << 1
        LSRS R2,R0,#12 // Got TS, but with 2 extra random hi bits
        ADR  R4,COM_S2b
        LDR  PC,[R4,R2,LSL #2]
.align 4
COM_S2b: .WORD COM_S2b_R+1, COM_S2b_I+1, COM_S2b_L+1, COM_S2b_II+1 // random bits 00
         .WORD COM_S2b_R+1, COM_S2b_I+1, COM_S2b_L+1, COM_S2b_II+1 // random bits 01
         .WORD COM_S2b_R+1, COM_S2b_I+1, COM_S2b_L+1, COM_S2b_II+1 // random bits 10
         .WORD COM_S2b_R+1, COM_S2b_I+1, COM_S2b_L+1, COM_S2b_II+1 // random bits 11
.align 4
COM_S2b_R:
        ADD  R5,R11 // SRCE = WP + S;
        B    COM_S2b_F2
.align 4
COM_S2b_L:
        LDRH R2,[R8,R3] // SRCE = (memory [PC + 0] << 8) | memory [PC + 1];
        ADDS R3,#2
        REV16 R2,R2
        CBZ  R5,COM_S2b_F  // if (S) // Not R0?
        LDRH R4,[R11,R5] // SRCE += (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R4,R4
        ADD  R2,R4
        UXTH R2,R2
        B    COM_S2b_F
.align 4
COM_S2b_II:
        LDRH R2,[R11,R5] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R2,R2
        ADDS R4,R2,#2
        REV16 R4,R4
        STRH R4,[R11,R5]
        B    COM_S2b_F
.align 4
COM_S2b_I:
        LDRH R2,[R11,R5] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R2,R2
COM_S2b_F:
        BICS R5,R2,#1 // SRCE &= 0xFFFE
        ADD  R5,R8
COM_S2b_F2:
// *********************************************************************************************
        LSLS R6,#22
        LSRS R6,#28
        LSLS R6,#1  // Got D << 1
        ADD  R6,R11 // DEST = WP + D;
        LDRH R5,[R5,#0]
        REV16 R5,R5 // SRCE = (memory [SRCE + 0] << 8) | memory [SRCE + 1];
        LDRH R4,[R6,#0]
        REV16 R4,R4 // TEMP = (memory [DEST + 0] << 8) | memory [DEST + 1];
        MOV  PC,R7
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567 (Format 9)
//DDTsSSSS00110oDD
.align 4
OP_3LO: LSRS R2,R0,#13 // if ((BYTE & 0x30) == 0x20) Ts==2 (already checked Ts high bit is 1)
        BCS  start // Carry Set - then it's not == 0x20
        ADDS R3,#2 // PC += 2; // Skip CRU's 2nd word address
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567 (Format 2)
//dddddddd0001oooo
.align 4
I_JMP:  SXTH R0,R0 // R0 now has a signed offset
        ASRS R0,#7 // Word offset
        ADDS R4,R0,#2 // Does this jump to itself?
        BNE  dojmp2   // Else quit the emulation
// *********************************************************************************************
I_IDLE: ADR  R0,RETCODE
        STR  R3,[R0,#0] // PC

        POP  {R0-R7}
        MOV  R8,R0
        MOV  R9,R1
        MOV  R10,R2
        MOV  R11,R3
        ADR  R0,RETCODE
        LDR  R0,[R0,#0]
        POP  {PC}
.align 4
RETCODE: .WORD 0
// *********************************************************************************************
.align 4
I_JLT:  TST  R1,#0x60 // AG|EQ
        BNE  start  // At least one of them was not zero
        B    dojump
// *********************************************************************************************
.align 4
I_JLE:  LSRS R4,R1,#6 // EQ -> Carry
        BCS  dojump
        LSRS R4,R1,#8 // LG -> Carry
        BCS  start
        B    dojump
// *********************************************************************************************
.align 4
I_JEQ:  LSRS R4,R1,#6 // EQ -> Carry
        BCC  start
        B    dojump
// *********************************************************************************************
.align 4
I_JHE:  TST  R1,#0xA0 // LG|EQ
        BEQ  start // Both were zero
        B    dojump
// *********************************************************************************************
.align 4
I_JGT:  LSRS R4,R1,#7 // AG -> Carry
        BCC  start
        B    dojump
// *********************************************************************************************
.align 4
I_JNE:  LSRS R4,R1,#6 // EQ -> Carry
        BCS  start
        B    dojump
// *********************************************************************************************

// Sticking this in the middle of the code, so we can get back to "start" from anywhere easily

// void run9900 (unsigned char * memory, unsigned short pc, unsigned short wp, unsigned char * buf);
// R0 =BYTE|INST
// R1 =ST
// R2 =SPARE
// R3 =PC
// R4 =TEMP (in some cases)
// R5 =SRCE
// R6 =DEST (initially INST|BYTE)
// R7 =2nd JUMP location
// R8 =memory
// R9 =regx38
// R10=SPARE
// R11=WP

// *********************************************************************************************
.align 4
run9900:
        PUSH {LR}

        PUSH {R4-R7} // R7..R4
        MOV  R4,R8
        MOV  R5,R9
        MOV  R6,R10
        MOV  R7,R11
        PUSH {R4-R7} // R11..R8

        MOV  R8,R0  // memory
        MOV  R9,R3  // regx38
        MOVS R3,R1  // PC
        ADDS R2,R0
        MOV  R11,R2 // WP
        MOVS R1,#0  // ST=0
        B    start
// *********************************************************************************************
.align 4
dojump: SXTH R0,R0 // R0 now has a signed offset
        ASRS R0,#7 // Word offset
dojmp2: ADDS R3,R0 // Special entry point just for JMP
// *********************************************************************************************
start:  UXTH R3,R3 // Discard any extra - but allow to flow into register space

        LDRB R0,[R9,#0]
        LSRS R0,#1
        BCC  I_IDLE

        LDRH R0,[R8,R3] // Get instruction BYTE|INST (LE format)
        ADDS R3,#2

startX: REV16 R6,R0    // R6=INST|BYTE
        LSRS R2,R6,#5  // Get opcode
        LSLS R2,#3     // x8
        ADR  R4,JMPTBL // This is just in reach
        ADDS R4,R2
        LDM  R4,{R7,PC} // Get 1st and 2nd branch locations (if it's needed)

// *********************************************************************************************
.align 4
I_JNC:  LSRS R4,R1,#5 // C -> Carry
        BCS  start
        B    dojump
// *********************************************************************************************
.align 4
I_JOC:  LSRS R4,R1,#5 // C -> Carry
        BCC  start
        B    dojump
// *********************************************************************************************
.align 4
I_JNO:  LSRS R4,R1,#4 // O -> Carry
        BCS  start
        B    dojump
// *********************************************************************************************
.align 4
I_JL:   TST  R1,#0xA0 // LG|EQ
        BNE  start // At least one of them was not zero
        B    dojump
// *********************************************************************************************
.align 4
I_JH:   LSRS R4,R1,#6 // EQ -> Carry
        BCS  start
        LSRS R4,R1,#8 // LG -> Carry
        BCC  start
        B    dojump
// *********************************************************************************************
.align 4
I_JOP:  LSRS R4,R1,#3 // P -> Carry
        BCC  start
        B    dojump
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//DDTsSSSSoooBTdDD
//R6 (BE)
//0123456789ABCDEF
//oooBTdDDDDTsSSSS
// *********************************************************************************************
.align 4
sdecode2d: // calculate S & SRCE
        LSLS R5,R6,#28
        LSRS R5,#27 // Got S << 1
        LSRS R2,R0,#10
        MOVS R4,#0x0C
        ANDS R2,R4 // Got TS * 4
        ANDS R0,R4 // Got TD * 4
        ADR  R4,COM_S2d
        LDR  PC,[R4,R2]
.align 4
COM_S2d: .WORD COM_S2d_R+1, COM_S2d_I+1, COM_S2d_L+1, COM_S2d_II+1
.align 4
COM_S2d_R:
        ADD  R5,R11 // SRCE = WP + S;
        B    COM_S2d_F2
.align 4
COM_S2d_L:
        LDRH R2,[R8,R3] // SRCE = (memory [PC + 0] << 8) | memory [PC + 1];
        ADDS R3,#2
        REV16 R2,R2
        CBZ  R5,COM_S2d_F  // if (S) // Not R0?
        LDRH R4,[R11,R5] // SRCE += (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R4,R4
        ADD  R2,R4
        UXTH R2,R2
        B    COM_S2d_F
.align 4
COM_S2d_II:
        LDRH R2,[R11,R5] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R2,R2
        ADDS R4,R2,#2
        REV16 R4,R4
        STRH R4,[R11,R5]
        B    COM_S2d_F
.align 4
COM_S2d_I:
        LDRH R2,[R11,R5] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R2,R2
COM_S2d_F:
        BICS R5,R2,#1 // SRCE &= 0xFFFE
        ADD  R5,R8
COM_S2d_F2:
// *********************************************************************************************
        LDRH R5,[R5,#0] // R5=SRCE - but note this is reversed byte order - instructions to fix.
// *********************************************************************************************
        LSLS R6,#22
        LSRS R6,#28
        LSLS R6,#1 // Got D << 1
        ADR  R4,COM_D2
        LDR  PC,[R4,R0]
.align 4
COM_D2: .WORD COM_D2_R+1, COM_D2_I+1, COM_D2_L+1, COM_D2_II+1
.align 4
COM_D2_R:
        ADD  R6,R11 // DEST = WP + D;
        MOV  PC,R7
.align 4
COM_D2_L:
        LDRH R2,[R8,R3] // DEST = (memory [PC + 0] << 8) | memory [PC + 1];
        ADDS R3,#2
        REV16 R2,R2
        CBZ  R6,COM_D2_F  // if (D) // Not R0?
        LDRH R4,[R11,R6] // DEST += (memory [WP + 0 + D] << 8) | memory [WP + 1 + D];
        REV16 R4,R4
        ADD  R2,R4
        UXTH R2,R2
COM_D2_F:
        BICS R6,R2,#1 // DEST &= 0xFFFE
        ADD  R6,R8
        MOV  PC,R7
.align 4
COM_D2_II:
        LDRH R2,[R11,R6] // DEST = (memory [WP + 0 + D] << 8) | memory [WP + 1 + D];
        REV16 R2,R2
        ADDS R4,R2,#2
        REV16 R4,R4
        STRH R4,[R11,R6]
        BICS R6,R2,#1 // DEST &= 0xFFFE
        ADD  R6,R8
        MOV  PC,R7
.align 4
COM_D2_I:
        LDRH R6,[R11,R6] // DEST = (memory [WP + 0 + D] << 8) | memory [WP + 1 + D];
        REV16 R6,R6
        BICS R6,#1 // DEST &= 0xFFFE
        ADD  R6,R8
        MOV  PC,R7

// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//DDTsSSSSoooBTdDD
//R6 (BE)
//0123456789ABCDEF
//oooBTdDDDDTsSSSS
// *********************************************************************************************
.align 4
sdecode1d: // calculate S & SRCE
        LSLS R5,R6,#28
        LSRS R5,#27 // Got S << 1
        LSRS R2,R0,#10
        MOVS R4,#0x0C
        ANDS R2,R4 // Got TS * 4
        ANDS R0,R4 // Got TD * 4
        ADR  R4,COM_S1
        LDR  PC,[R4,R2]
.align 4
COM_S1: .WORD COM_S1_R+1, COM_S1_I+1, COM_S1_L+1, COM_S1_II+1
.align 4
COM_S1_R:
        LDRB R5,[R11,R5] // R5=SRCE (WP + S;)
        B    COM_S1_F2
.align 4
COM_S1_L:
        LDRH R2,[R8,R3] // SRCE = (memory [PC + 0] << 8) | memory [PC + 1];
        ADDS R3,#2
        REV16 R2,R2
        CBZ  R5,COM_S1_F  // if (S) // Not R0?
        LDRH R4,[R11,R5] // SRCE += (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R4,R4
        ADD  R2,R4
        UXTH R2,R2
        B    COM_S1_F
.align 4
COM_S1_II:
        LDRH R2,[R11,R5] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R2,R2
        ADDS R4,R2,#1
        REV16 R4,R4
        STRH R4,[R11,R5]
        B    COM_S1_F
.align 4
COM_S1_I:
        LDRH R2,[R11,R5] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R2,R2
COM_S1_F:
        ADD  R2,R8
// *********************************************************************************************
        LDRB R5,[R2,#0] // R5=SRCE
// *********************************************************************************************
COM_S1_F2:
        LSLS R6,#22
        LSRS R6,#28
        LSLS R6,#1 // Got D << 1
        ADR  R4,COM_D1
        LDR  PC,[R4,R0]
.align 4
COM_D1: .WORD COM_D1_R+1, COM_D1_I+1, COM_D1_L+1, COM_D1_II+1
.align 4
COM_D1_R:
        ADD  R6,R11 // DEST = WP + D;
        MOV  PC,R7
.align 4
COM_D1_L:
        LDRH R2,[R8,R3] // DEST = (memory [PC + 0] << 8) | memory [PC + 1];
        ADDS R3,#2
        REV16 R2,R2
        CBZ  R6,COM_D1_F  // if (D) // Not R0?
        LDRH R4,[R11,R6] // DEST += (memory [WP + 0 + D] << 8) | memory [WP + 1 + D];
        REV16 R4,R4
        ADD  R2,R4
        UXTH R6,R2
        ADD  R6,R8
        MOV  PC,R7
.align 4
COM_D1_F:
        ADD  R6,R2,R8
        MOV  PC,R7
.align 4
COM_D1_II:
        LDRH R2,[R11,R6] // DEST = (memory [WP + 0 + D] << 8) | memory [WP + 1 + D];
        REV16 R2,R2
        ADDS R4,R2,#1
        REV16 R4,R4
        STRH R4,[R11,R6]
        ADD  R6,R2,R8
        MOV  PC,R7
.align 4
COM_D1_I:
        LDRH R6,[R11,R6] // DEST = (memory [WP + 0 + D] << 8) | memory [WP + 1 + D];
        REV16 R6,R6
        ADD  R6,R8
        MOV  PC,R7

// *********************************************************************************************

// F18A Instructions (part II)

// *********************************************************************************************
.align 4
I_XOP: // F18A PIX instruction
        LSLS R7,R4,#17  // Move leftmost bit (M) into the Carry flag
        BCC  I_XOP_BL   // Not a BM calculation, goto BL code instead

        UXTB R4,R5      // R4=00000000 yyyyyyyy (E/A 336)
        LSLS R2,R4,#5   // R2=000yyyyy yyy00000
        ORRS R2,R4      // R2=000yyyyy zzzyyyyy
        MOVS R7,#0xF8
        BICS R2,R7      // R2=000yyyyy 00000yyy

        LSRS R4,R5,#8   // R4=00000000 xxxxxxxx
        MOVS R7,#7
        BICS R4,R7      // R4=00000000 xxxxx000
        ORRS R2,R4      // R2=000yyyyy xxxxxyyy (byte offset)

        ADR  R7,VREGSL
        LDR  R7,[R7,#0] // Get offset
        ADD  R7,R8      // Within the 64KB memory area
        LDRB R4,[R7,#4] // Get VR04
        MOVS R7,#0x04   // BM Pattern Table Location
        ANDS R4,R7      // Can only be >0000 or >2000 (E/A 335)
        LSLS R4,#11     // Move bit into position
        ORRS R2,R4

        REV16 R2,R2
        STRH R2,[R6,#0] // *DEST=R2
        B    start
/*
.align 4
I_XOP_BL: // Return to C code function: F18A_PIX for BL mode
        MOVS R7,R1    // Save ST
        MOV  R10,R3   // Save current PC
        MOVS R1,R5    // R1=SRCE (X|Y)
        MOV  R2,R4    // R2=TEMP (*DEST) Flags and Bits
        MOV  R3,R6    // R3=DEST (pointer into memory [])
        BLX  F18A_PIX // R0 has the reversed opcode: BYTE|INST
        MOV  R3,R10   // Restore current PC
        MOVS R1,R7    // Restore ST
        B    start
*/
.align 4
I_XOP_BL:
        MOV  R10,R3      // Save current PC
        ADR  R3,VREGSH
        LDR  R3,[R3,#0]  // Get offset
        ADD  R3,R8       // Within the 64KB memory area
        LDRB R0,[R3,#3]  // Get VR35

        MOVS R7,R5
        MOVS R2,#255     // Set up a mask, plus a base for > 255 numbers
        ANDS R7,R2       // Y
        CMP  R0,#0       // 0 is special, == 256
        BNE  I_XOP_BL_M
        ADDS R0,R2,#1    // 256 - R2 is now free
I_XOP_BL_M:
        MULS R0,R7       // R7 is now free
        LSRS R5,R5,#8    // X
        ADDS R0,R5       // R0 = p = (y * vregs [35]) + x; - R5 is now free

        LDRB R2,[R3,#0]  // Get VR32 - R3 is now free
        LSLS R2,#6
        LSRS R5,R0,#2
        ADDS R2,R5       // R2 = a = (vregs [32] << 6) + (p >> 2);

        LSRS R5,R4,#15   // if (R4 & PIX_A) - PIX_A into Carry
        BCC  I_XOP_BL_P  // Clear = Process Bits, else just store address result..

        REV16 R2,R2
        STRH R2,[R6,#0]
        MOV  R3,R10      // Restore current PC
        B    start

.align 4
I_XOP_BL_P:
        LSLS R0,#30
        LSRS R0,#30      // R0 = s = p & 0x03;
        ADD  R2,R8       // Within the 64KB memory area
        LDRB R3,[R2,#0]  // R3 = b = mem [a];
        ADR  R7,MSKSH
        ADDS R7,R0       // R0 is now free
        LDRB R0,[R7,#0]  // Get MASK
        ANDS R0,R3
        LDRB R5,[R7,#4]  // Get SHIFT
        LSRS R0,R5       // R0 = p = (b & MSKSH [s]) >> MSKSH [s + 4]; - R5 is now free

        LSRS R5,R4,#11   // if ((R4 & PIX_W) == 0)
        BCS  I_XOP_BL_NW
        LSRS R5,R4,#10   // if (R4 & PIX_C)
        BCC  I_XOP_BL_W
        LSRS R5,R4,#9    // if (R4 & PIX_E)
        BCC  I_XOP_BL_ET // Equal or Not Equal Test?

        LSLS R5,R4,#26
        LSRS R5,#30      // ((R4 >> 4) & PIX_PP)
        CMP  R5,R0
        BNE  I_XOP_BL_NW // if (p != ((R4 >> 4) & PIX_PP)) goto nowrite;
        B    I_XOP_BL_W

.align 4
I_XOP_BL_ET: // Equal Test
        LSLS R5,R4,#26
        LSRS R5,#30      // ((R4 >> 4) & PIX_PP)
        CMP  R5,R0
        BEQ  I_XOP_BL_NW // if (p == ((R4 >> 4) & PIX_PP)) goto nowrite;

I_XOP_BL_W:

// R0 = p = original 2 pixel colour bits shifted right
// R1 = STATUS
// R2 = VDP RAM byte location
// R3 = original byte
// R4 = Flags and Bits
// R5 = *** Free register ***
// R6 = DEST
// R7 = MSKSH pointer

        LDRB R5,[R7,#0]  // Get MASK
        BICS R3,R5
        LDRB R7,[R7,#4]  // Get SHIFT
        LSLS R5,R4,#30
        LSRS R5,#30
        LSLS R5,R7
        ORRS R3,R5
        STRB R3,[R2,#0]  // mem [a] = (b & ~MSKSH [s]) | ((R4 & PIX_PP) << MSKSH [s + 4]);

I_XOP_BL_NW:
        LSRS R5,R4,#12   // if (R4 & PIX_R)
        BCC  I_XOP_BL_F
        MOVS R5,#3
        BICS R4,R5
        ORRS R4,R0       // R4 = (R4 & ~PIX_PP) | p;
        REV16 R4,R4
        STRH R4,[R6,#0]
I_XOP_BL_F:
        MOV  R3,R10      // Restore current PC
        B    start

.align 4
VREGSL: .WORD 0x6000 // Video Registers map point (VR0)
VREGSH: .WORD 0x6020 // Video Registers map point (VR32)
MSKSH:  .BYTE 0xC0, 0x30, 0x0C, 0x03, 0x06, 0x04, 0x02, 0x00
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567 (Format 7)
//oo00000000001100
.align 4
I_RET:  MOV  R0,R11 // WP
        LDRH R4,[R0,#30] // R15
        REV16 R4,R4
        BICS R4,#1 // R4 &= 0xFFFE

        ADDS R4,#2
        UXTH R4,R4
        REV16 R2,R4
        STRH R2,[R0,#30] // R15

        LDRH R3,[R8,R4] // Get the value off the stack
        REV16 R3,R3
        BICS R3,#1 // R3 &= 0xFFFE
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567 (Format 6)
//10TsSSSS00001100
.align 4
I_CALL: MOV  R0,R11 // WP
        LDRH R4,[R0,#30] // Get R15
        REV16 R4,R4
        BICS R4,#1 // R4 &= 0xFFFE

        SUBS R2,R4,#2 // Stack word has been consumed
        REV16 R2,R2
        STRH R2,[R0,#30] // Store new R15

        REV16 R3,R3
        STRH R3,[R8,R4] // Old *R15

        SUBS R3,R5,R8 // Normalise
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567 (Format 6)
//00TsSSSS00001101
.align 4
I_PUSH: LDRH R5,[R5,#0] // R5=SRCE

        MOV  R0,R11 // WP
        LDRH R4,[R0,#30] // Get R15
        REV16 R4,R4
        BICS R4,#1 // R4 &= 0xFFFE

        SUBS R2,R4,#2 // Stack word has been consumed
        REV16 R2,R2
        STRH R2,[R0,#30] // Store new R15

        STRH R5,[R8,R4] // Old *R15
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567 (Format 6)
//00TsSSSS00001111
.align 4
I_POP:  MOV  R0,R11 // WP
        LDRH R4,[R0,#30] // Get R15
        REV16 R4,R4
        BICS R4,#1 // R4 &= 0xFFFE

        ADDS R4,#2 // Stack word has been consumed
        UXTH R4,R4
        REV16 R2,R4
        STRH R2,[R0,#30] // Store new R15

        LDRH R0,[R8,R4] // *R15
        STRH R0,[R5,#0] // R5=DEST
        B    start
// *********************************************************************************************
.align 4
JMPTBL:
        .WORD 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1   // >0000
        .WORD 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1   // >0100
        .WORD 0     +1, I_LI     +1, 0     +1, I_AI     +1, 0     +1, I_ANDI   +1, 0     +1, I_ORI    +1, 0     +1, I_CI     +1, 0     +1, I_STWP   +1, 0     +1, I_STST   +1, 0     +1, I_LWPI   +1   // >0200
        .WORD 0     +1, I_LIMI   +1, 0     +1, start    +1, 0     +1, I_IDLE   +1, 0     +1, start    +1, 0     +1, I_RTWP   +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1   // >0300
        .WORD I_BLWP+1, sdecode2a+1, I_BLWP+1, sdecode2a+1, I_B   +1, sdecode2a+1, I_B   +1, sdecode2a+1, I_X   +1, sdecode2a+1, I_X   +1, sdecode2a+1, I_CLR +1, sdecode2a+1, I_CLR +1, sdecode2a+1   // >0400
        .WORD I_NEG +1, sdecode2a+1, I_NEG +1, sdecode2a+1, I_INV +1, sdecode2a+1, I_INV +1, sdecode2a+1, I_INC +1, sdecode2a+1, I_INC +1, sdecode2a+1, I_INCT+1, sdecode2a+1, I_INCT+1, sdecode2a+1   // >0500
        .WORD I_DEC +1, sdecode2a+1, I_DEC +1, sdecode2a+1, I_DECT+1, sdecode2a+1, I_DECT+1, sdecode2a+1, I_BL  +1, sdecode2a+1, I_BL  +1, sdecode2a+1, I_SWPB+1, sdecode2a+1, I_SWPB+1, sdecode2a+1   // >0600
        .WORD I_SETO+1, sdecode2a+1, I_SETO+1, sdecode2a+1, I_ABS +1, sdecode2a+1, I_ABS +1, sdecode2a+1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1   // >0700
        .WORD 0     +1, I_SRA    +1, 0     +1, I_SRA    +1, 0     +1, I_SRA    +1, 0     +1, I_SRA    +1, 0     +1, I_SRA    +1, 0     +1, I_SRA    +1, 0     +1, I_SRA    +1, 0     +1, I_SRA    +1   // >0800
        .WORD 0     +1, I_SRL    +1, 0     +1, I_SRL    +1, 0     +1, I_SRL    +1, 0     +1, I_SRL    +1, 0     +1, I_SRL    +1, 0     +1, I_SRL    +1, 0     +1, I_SRL    +1, 0     +1, I_SRL    +1   // >0900
        .WORD 0     +1, I_SLA    +1, 0     +1, I_SLA    +1, 0     +1, I_SLA    +1, 0     +1, I_SLA    +1, 0     +1, I_SLA    +1, 0     +1, I_SLA    +1, 0     +1, I_SLA    +1, 0     +1, I_SLA    +1   // >0A00
        .WORD 0     +1, I_SRC    +1, 0     +1, I_SRC    +1, 0     +1, I_SRC    +1, 0     +1, I_SRC    +1, 0     +1, I_SRC    +1, 0     +1, I_SRC    +1, 0     +1, I_SRC    +1, 0     +1, I_SRC    +1   // >0B00
        .WORD 0     +1, I_RET    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, I_CALL+1, sdecode2a+1, I_CALL+1, sdecode2a+1, 0     +1, start    +1, 0     +1, start    +1   // >0C00
        .WORD I_PUSH+1, sdecode2a+1, I_PUSH+1, sdecode2a+1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1   // >0D00
        .WORD 0     +1, I_SLC    +1, 0     +1, I_SLC    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1   // >0E00
        .WORD I_POP +1, sdecode2a+1, I_POP +1, sdecode2a+1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1, 0     +1, start    +1   // >0F00
        .WORD 0     +1, I_JMP    +1, 0     +1, I_JMP    +1, 0     +1, I_JMP    +1, 0     +1, I_JMP    +1, 0     +1, I_JMP    +1, 0     +1, I_JMP    +1, 0     +1, I_JMP    +1, 0     +1, I_JMP    +1   // >1000
        .WORD 0     +1, I_JLT    +1, 0     +1, I_JLT    +1, 0     +1, I_JLT    +1, 0     +1, I_JLT    +1, 0     +1, I_JLT    +1, 0     +1, I_JLT    +1, 0     +1, I_JLT    +1, 0     +1, I_JLT    +1   // >1100
        .WORD 0     +1, I_JLE    +1, 0     +1, I_JLE    +1, 0     +1, I_JLE    +1, 0     +1, I_JLE    +1, 0     +1, I_JLE    +1, 0     +1, I_JLE    +1, 0     +1, I_JLE    +1, 0     +1, I_JLE    +1   // >1200
        .WORD 0     +1, I_JEQ    +1, 0     +1, I_JEQ    +1, 0     +1, I_JEQ    +1, 0     +1, I_JEQ    +1, 0     +1, I_JEQ    +1, 0     +1, I_JEQ    +1, 0     +1, I_JEQ    +1, 0     +1, I_JEQ    +1   // >1300
        .WORD 0     +1, I_JHE    +1, 0     +1, I_JHE    +1, 0     +1, I_JHE    +1, 0     +1, I_JHE    +1, 0     +1, I_JHE    +1, 0     +1, I_JHE    +1, 0     +1, I_JHE    +1, 0     +1, I_JHE    +1   // >1400
        .WORD 0     +1, I_JGT    +1, 0     +1, I_JGT    +1, 0     +1, I_JGT    +1, 0     +1, I_JGT    +1, 0     +1, I_JGT    +1, 0     +1, I_JGT    +1, 0     +1, I_JGT    +1, 0     +1, I_JGT    +1   // >1500
        .WORD 0     +1, I_JNE    +1, 0     +1, I_JNE    +1, 0     +1, I_JNE    +1, 0     +1, I_JNE    +1, 0     +1, I_JNE    +1, 0     +1, I_JNE    +1, 0     +1, I_JNE    +1, 0     +1, I_JNE    +1   // >1600
        .WORD 0     +1, I_JNC    +1, 0     +1, I_JNC    +1, 0     +1, I_JNC    +1, 0     +1, I_JNC    +1, 0     +1, I_JNC    +1, 0     +1, I_JNC    +1, 0     +1, I_JNC    +1, 0     +1, I_JNC    +1   // >1700
        .WORD 0     +1, I_JOC    +1, 0     +1, I_JOC    +1, 0     +1, I_JOC    +1, 0     +1, I_JOC    +1, 0     +1, I_JOC    +1, 0     +1, I_JOC    +1, 0     +1, I_JOC    +1, 0     +1, I_JOC    +1   // >1800
        .WORD 0     +1, I_JNO    +1, 0     +1, I_JNO    +1, 0     +1, I_JNO    +1, 0     +1, I_JNO    +1, 0     +1, I_JNO    +1, 0     +1, I_JNO    +1, 0     +1, I_JNO    +1, 0     +1, I_JNO    +1   // >1900
        .WORD 0     +1, I_JL     +1, 0     +1, I_JL     +1, 0     +1, I_JL     +1, 0     +1, I_JL     +1, 0     +1, I_JL     +1, 0     +1, I_JL     +1, 0     +1, I_JL     +1, 0     +1, I_JL     +1   // >1A00
        .WORD 0     +1, I_JH     +1, 0     +1, I_JH     +1, 0     +1, I_JH     +1, 0     +1, I_JH     +1, 0     +1, I_JH     +1, 0     +1, I_JH     +1, 0     +1, I_JH     +1, 0     +1, I_JH     +1   // >1B00
        .WORD 0     +1, I_JOP    +1, 0     +1, I_JOP    +1, 0     +1, I_JOP    +1, 0     +1, I_JOP    +1, 0     +1, I_JOP    +1, 0     +1, I_JOP    +1, 0     +1, I_JOP    +1, 0     +1, I_JOP    +1   // >1C00
        .WORD 0     +1, I_SBO    +1, 0     +1, I_SBO    +1, 0     +1, I_SBO    +1, 0     +1, I_SBO    +1, 0     +1, I_SBO    +1, 0     +1, I_SBO    +1, 0     +1, I_SBO    +1, 0     +1, I_SBO    +1   // >1D00
        .WORD 0     +1, I_SBZ    +1, 0     +1, I_SBZ    +1, 0     +1, I_SBZ    +1, 0     +1, I_SBZ    +1, 0     +1, I_SBZ    +1, 0     +1, I_SBZ    +1, 0     +1, I_SBZ    +1, 0     +1, I_SBZ    +1   // >1E00
        .WORD 0     +1, I_TB     +1, 0     +1, I_TB     +1, 0     +1, I_TB     +1, 0     +1, I_TB     +1, 0     +1, I_TB     +1, 0     +1, I_TB     +1, 0     +1, I_TB     +1, 0     +1, I_TB     +1   // >1F00
        .WORD I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1   // >2000
        .WORD I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1   // >2100
        .WORD I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1   // >2200
        .WORD I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1, I_COC +1, sdecode2b+1   // >2300
        .WORD I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1   // >2400
        .WORD I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1   // >2500
        .WORD I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1   // >2600
        .WORD I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1, I_CZC +1, sdecode2b+1   // >2700
        .WORD I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1   // >2800
        .WORD I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1   // >2900
        .WORD I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1   // >2A00
        .WORD I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1, I_XOR +1, sdecode2b+1   // >2B00
        .WORD I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1   // >2C00
        .WORD I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1   // >2D00
        .WORD I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1   // >2E00
        .WORD I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1, I_XOP +1, sdecode2b+1   // >2F00
        .WORD 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1   // >3000
        .WORD 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1   // >3100
        .WORD 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1   // >3200
        .WORD 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1   // >3300
        .WORD 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1   // >3400
        .WORD 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1   // >3500
        .WORD 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1   // >3600
        .WORD 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1, 0     +1, start    +1, 0     +1, OP_3LO   +1   // >3700
        .WORD I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1   // >3800
        .WORD I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1   // >3900
        .WORD I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1   // >3A00
        .WORD I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1, I_MUL +1, sdecode2b+1   // >3B00
        .WORD I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1   // >3C00
        .WORD I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1   // >3D00
        .WORD I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1   // >3E00
        .WORD I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1, I_DIV +1, sdecode2b+1   // >3F00
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4000
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4100
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4200
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4300
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4400
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4500
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4600
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4700
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4800
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4900
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4A00
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4B00
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4C00
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4D00
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4E00
        .WORD I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1, I_SZC +1, sdecode2d+1   // >4F00
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5000
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5100
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5200
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5300
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5400
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5500
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5600
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5700
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5800
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5900
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5A00
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5B00
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5C00
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5D00
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5E00
        .WORD I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1, I_SZCB+1, sdecode1d+1   // >5F00
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6000
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6100
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6200
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6300
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6400
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6500
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6600
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6700
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6800
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6900
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6A00
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6B00
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6C00
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6D00
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6E00
        .WORD I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1, I_S   +1, sdecode2d+1   // >6F00
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7000
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7100
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7200
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7300
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7400
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7500
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7600
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7700
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7800
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7900
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7A00
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7B00
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7C00
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7D00
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7E00
        .WORD I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1, I_SB  +1, sdecode1d+1   // >7F00
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8000
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8100
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8200
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8300
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8400
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8500
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8600
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8700
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8800
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8900
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8A00
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8B00
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8C00
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8D00
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8E00
        .WORD I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1, I_C   +1, sdecode2d+1   // >8F00
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9000
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9100
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9200
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9300
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9400
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9500
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9600
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9700
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9800
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9900
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9A00
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9B00
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9C00
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9D00
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9E00
        .WORD I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1, I_CB  +1, sdecode1d+1   // >9F00
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >A000
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >A100
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >A200
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >A300
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >A400
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >A500
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >A600
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >A700
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >A800
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >A900
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >AA00
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >AB00
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >AC00
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >AD00
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >AE00
        .WORD I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1, I_A   +1, sdecode2d+1   // >AF00
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >B000
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >B100
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >B200
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >B300
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >B400
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >B500
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >B600
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >B700
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >B800
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >B900
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >BA00
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >BB00
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >BC00
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >BD00
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >BE00
        .WORD I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1, I_AB  +1, sdecode1d+1   // >BF00
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >C000
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >C100
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >C200
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >C300
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >C400
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >C500
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >C600
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >C700
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >C800
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >C900
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >CA00
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >CB00
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >CC00
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >CD00
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >CE00
        .WORD I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1, I_MOV +1, sdecode2d+1   // >CF00
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >D000
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >D100
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >D200
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >D300
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >D400
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >D500
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >D600
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >D700
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >D800
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >D900
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >DA00
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >DB00
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >DC00
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >DD00
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >DE00
        .WORD I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1, I_MOVB+1, sdecode1d+1   // >DF00
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >E000
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >E100
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >E200
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >E300
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >E400
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >E500
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >E600
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >E700
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >E800
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >E900
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >EA00
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >EB00
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >EC00
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >ED00
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >EE00
        .WORD I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1, I_SOC +1, sdecode2d+1   // >EF00
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >F000
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >F100
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >F200
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >F300
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >F400
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >F500
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >F600
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >F700
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >F800
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >F900
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >FA00
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >FB00
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >FC00
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >FD00
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >FE00
        .WORD I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1, I_SOCB+1, sdecode1d+1   // >FF00

/*
 * Project: pico9918
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 * 
 * Purpose: TMS9900 GPU core
 * 
 * Author: JasonACT (AtariAge)
 *
 */

.syntax unified // ARM unified syntax
.thumb_func     // Code uses Thumb instructions

.section .data  // In RAM Please

.global run9900 // Entry point
//.global instscnt // Global instruction counter

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
        MOVS R4,#0x0F  // Mask lower nibble
        ANDS R2,R4
        BNE  I_SRA_nr0 // Result not 0 - got it, else
        MOVS R2,#16
I_SRA_nr0:
        LDRH R0,[R5,R6]
        REVSH R0,R0 // Signed

        MOVS R4,#0x0E // Get initialised ST
        ANDS R1,R4
        
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
        MOVS R4,#0x0F  // Mask lower nibble
        ANDS R2,R4
        BNE  I_SRL_nr0 // Result not 0 - got it, else
        MOVS R2,#16
I_SRL_nr0:
        LDRH R0,[R5,R6]
        REV16 R0,R0 // Unsigned

        MOVS R4,#0x0E // Get initialised ST
        ANDS R1,R4
        
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

        MOVS R4,#0x06 // Get initialised ST
        ANDS R1,R4

        LSRS R2,R0,#12 // Get Shift Count
        BNE  I_SLA_nr0 // Result not 0 - got it, else
        LDRB R2,[R6,#1] // Get R0 low byte
        MOVS R4,#0x0F  // Mask lower nibble
        ANDS R2,R4
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
        MOVS R4,#0x0F  // Mask lower nibble
        ANDS R2,R4
        BNE  I_SRC_nr0 // Result not 0 - got it, else
        MOVS R2,#16
I_SRC_nr0:
        LDRH R0,[R5,R6]
        REV16 R0,R0 // Unsigned
        LSLS R4,R0,#16
        ORRS R0,R4 // Copy lower halfword to high halfword

        MOVS R4,#0x0E // Get initialised ST
        ANDS R1,R4
        
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
        MOVS R4,#0x0F  // Mask lower nibble
        ANDS R2,R4
        BNE  I_SLC_nr0 // Result not 0 - got it, else
        MOVS R2,#16
I_SLC_nr0:
        LDRH R0,[R5,R6]
        REV16 R0,R0 // Unsigned
        LSLS R4,R0,#16
        ORRS R0,R4 // Copy lower halfword to high halfword

        MOVS R4,#0x0E // Get initialised ST
        ANDS R1,R4

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
        LSRS R6,#27
        ADD  R6,R11 // DEST = WP + ((BYTE & 0x0F) << 1);
        MOV  R2,R8
        LDRH R0,[R2,R3]
        ADDS R3,#2
        UXTH R3,R3
        STRH R0,[R6,#0]
        REV16 R0,R0
        MOVS R4,#0x1E // Get initialised ST
        ANDS R1,R4
        B    OP_COMP_W
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//ooo.WWWW00000010
.align 4
I_ANDI: LSLS R6,R0,#20
        LSRS R6,#27
        ADD  R6,R11 // DEST = WP + ((BYTE & 0x0F) << 1);
        MOV  R2,R8
        LDRH R0,[R2,R3]
        ADDS R3,#2
        UXTH R3,R3
        LDRH R5,[R6,#0] // R6=DEST
        ANDS R0,R5
        STRH R0,[R6,#0] // R6=DEST
        REV16 R0,R0
        MOVS R4,#0x1E // Get initialised ST
        ANDS R1,R4
        B    OP_COMP_W
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//ooo.WWWW00000010
.align 4
I_ORI:  LSLS R6,R0,#20
        LSRS R6,#27
        ADD  R6,R11 // DEST = WP + ((BYTE & 0x0F) << 1);
        MOV  R2,R8
        LDRH R0,[R2,R3]
        ADDS R3,#2
        UXTH R3,R3
        LDRH R5,[R6,#0] // R6=DEST
        ORRS R0,R5
        STRH R0,[R6,#0] // R6=DEST
        REV16 R0,R0
        MOVS R4,#0x1E // Get initialised ST
        ANDS R1,R4
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_NEG:  LDRH R0,[R5,#0] // R5=SRCE/DEST
        REV16 R0,R0

        MOVS R4,#0x06 // Get initialised ST
        ANDS R1,R4

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
        MOVS R4,#0x1E // Get initialised ST
        ANDS R1,R4
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_XOR:  EORS R4,R5 // R4=TEMP, R5=*SRCE
        REV16 R4,R4
        STRH R4,[R6,#0] // R6=DEST
        REV16 R0,R4
        MOVS R4,#0x1E // Get initialised ST
        ANDS R1,R4
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_SZC:  LDRH R0,[R6,#0] // R6=DEST
        BICS R0, R5
        STRH R0,[R6,#0]
        REV16 R0,R0
        MOVS R4,#0x1E // Get initialised ST
        ANDS R1,R4
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_SOC:  LDRH R0,[R6,#0]
        ORRS R0,R5
        STRH R0,[R6,#0]
        REV16 R0,R0
        MOVS R4,#0x1E // Get initialised ST
        ANDS R1,R4
        B    OP_COMP_W
// *********************************************************************************************
.align 4
I_ABS:  LDRH R0,[R5,#0] // R5=SRCE
        REV16 R0,R0

        MOVS R4,#0x06 // Get initialised ST
        ANDS R1,R4

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

        MOVS R4,#0x06 // Get initialised ST
        ANDS R1,R4

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

        MOVS R4,#0x06 // Get initialised ST
        ANDS R1,R4

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

        MOVS R4,#0x06 // Get initialised ST
        ANDS R1,R4

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

        MOVS R4,#0x06 // Get initialised ST
        ANDS R1,R4

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
        MOVS R4,#0x1E // Get initialised ST
        ANDS R1,R4
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
        MOV  R2,R8
        LDRH R5,[R2,R3]
        ADDS R3,#2
        UXTH R3,R3
// *********************************************************************************************
I_A:    LDRH R2,[R6,#0] // R6=DEST
        REV16 R2,R2
        REV16 R5,R5

        ADDS R7,R2,R5

        MOVS R4,#0x06 // Get initialised ST
        ANDS R1,R4

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

        MOVS R4,#0x06 // Get initialised ST
        ANDS R1,R4

        CMP  R5,#0
        BEQ  I_S_c // Zero sets Carry
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
        MOVS R4,#0x1A // Get initialised ST
        ANDS R1,R4
        B    OP_COMP_B
// *********************************************************************************************
.align 4
I_SOCB: LDRB R0,[R6,#0]
        ORRS R0,R5
        STRB R0,[R6,#0]
        MOVS R4,#0x1A // Get initialised ST
        ANDS R1,R4
        B    OP_COMP_B
// *********************************************************************************************
.align 4
I_AB:   LDRB R2,[R6,#0] // R6=DEST

        ADDS R0,R2,R5

        MOVS R4,#0x02 // Get initialised ST
        ANDS R1,R4

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

        MOVS R4,#0x02 // Get initialised ST
        ANDS R1,R4

        CMP  R5,#0
        BEQ  I_SB_c // Zero sets Carry
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
        MOVS R4,#0x1A // Get initialised ST
        ANDS R1,R4
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

        MOVS R4,#0x1E // Get initialised ST
        ANDS R1,R4

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

        MOVS R4,#0x1A // Get initialised ST
        ANDS R1,R4

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
        MOVS R4,#0x20 // EQ
        ORRS R1,R4
        B    start
.align 4
I_COC_ne:
        MOVS R4,#0xDF // ~EQ
        ANDS R1,R4
        B    start
// *********************************************************************************************
.align 4
I_CZC:  ANDS R4,R5
        BNE  I_CZC_ne
        MOVS R4,#0x20 // EQ
        ORRS R1,R4
        B    start
.align 4
I_CZC_ne:
        MOVS R4,#0xDF // ~EQ
        ANDS R1,R4
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//ooo.WWWW00000010
.align 4
I_CI:   LSLS R6,R0,#20
        LSRS R6,#27
        ADD  R6,R11 // DEST = WP + ((BYTE & 0x0F) << 1);
        MOV  R2,R8
        LDRH R5,[R2,R3]
        ADDS R3,#2
        UXTH R3,R3
        REV16 R5,R5

        LDRH R2,[R6,#0] // R6=DEST
        REV16 R2,R2

        MOVS R4,#0x1E // Get initialised ST
        ANDS R1,R4

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

        MOVS R2,#0xD0
        LSLS R2,#24 // -> 0xD0000000 = SIO_BASE
        STR  R4,[R2, #0x60] // SIO_DIV_UDIVIDEND_OFFSET
        STR  R5,[R2, #0x64] // SIO_DIV_UDIVISOR_OFFSET
        NOP
        NOP
        NOP
        MOVS R4,#0xF7 // ~OV
        ANDS R1,R4
        NOP
        NOP
        NOP
        LDR  R5,[R2, #0x74] // SIO_DIV_REMAINDER_OFFSET
        LDR  R4,[R2, #0x70] // SIO_DIV_QUOTIENT_OFFSET

        REV16 R4,R4
        STRH R4,[R6,#0]
        REV16 R5,R5
        STRH R5,[R6,#2]
        B    start
.align 4
I_DIV_ov:
        MOVS R4,#0x08 // OV
        ORRS R1,R4
        B    start
// *********************************************************************************************
.align 4
I_LWPI: MOV  R2,R8
        LDRH R0,[R2,R3]
        ADDS R3,#2
        UXTH R3,R3
        REV16 R0,R0
        LSRS R0,#1
        LSLS R0,#1 // R0 &= 0xFFFE
        ADD  R0,R8
        MOV  R11,R0
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//ooo.WWWW00000010
.align 4
I_STWP: LSLS R6,R0,#20
        LSRS R6,#27 // DEST = WP + ((BYTE & 0x0F) << 1);
        MOV  R0,R11
        MOV  R2,R8
        SUBS R4,R0,R2
        REV16 R4,R4
        STRH R4,[R6,R0]
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//ooo.WWWW00000010
.align 4
I_STST: LSLS R6,R0,#20
        LSRS R6,#27
        ADD  R6,R11 // DEST = WP + ((BYTE & 0x0F) << 1);
        STRH R1,[R6,#0] // Don't need to swap low bytes here
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567
//ooo.WWWW00000011
.align 4
I_LIMI: ADDS R3,#2    // Skip LIMI's 2nd WORD
        UXTH R3,R3
        B    start
// *********************************************************************************************
.align 4
I_BLWP: LDRH R0,[R5,#0] // R5=SRCE
        REV16 R0,R0
        LSRS R0,#1
        LSLS R0,#1 // R0 &= 0xFFFE
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
        LSRS R3,#1
        LSLS R3,#1 // R3 &= 0xFFFE
        B    start
// *********************************************************************************************
.align 4
I_RTWP: MOV  R5,R11  // Get address of WP

        LDRB R1,[R5,#30] // R15 high byte

        LDRH R3,[R5,#28] // R14..
        REV16 R3,R3
        LSRS R3,#1
        LSLS R3,#1 // R0 &= 0xFFFE

        LDRH R0,[R5,#26] // R13..
        REV16 R0,R0
        LSRS R0,#1
        LSLS R0,#1 // R0 &= 0xFFFE
        ADD  R0,R8
        MOV  R11,R0
        B    start
// *********************************************************************************************
.align 4
I_B:    MOV  R0,R8
        SUBS R3,R5,R0 // Normalise
        B    start
// *********************************************************************************************
.align 4
I_BL:   MOV  R2,R11
        REV16 R3,R3
        STRH R3,[R2,#22] // R11
        MOV  R4,R8
        SUBS R3,R5,R4 // Normalise
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
        MOVS R4,#0xDF // ~EQ
        ANDS R1,R4
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
        LSRS R2,R0,#12
        LSLS R2,#2 // Got TS * 4, but with 2 extra random hi bits
        ADR  R4,COM_S2a
        LDR  R4,[R4,R2]
        MOV  PC,R4
.align 4
COM_S2a: .WORD COM_S2a_R, COM_S2a_I, COM_S2a_L, COM_S2a_II // random bits 00
         .WORD COM_S2a_R, COM_S2a_I, COM_S2a_L, COM_S2a_II // random bits 01
         .WORD COM_S2a_R, COM_S2a_I, COM_S2a_L, COM_S2a_II // random bits 10
         .WORD COM_S2a_R, COM_S2a_I, COM_S2a_L, COM_S2a_II // random bits 11
.align 4
COM_S2a_R:
        ADD  R5,R11 // SRCE = WP + S;
        MOV  PC,R7
.align 4
COM_S2a_L:
        MOV  R2,R8
        LDRH R2,[R2,R3] // SRCE = (memory [PC + 0] << 8) | memory [PC + 1];
        ADDS R3,#2
        UXTH R3,R3
        REV16 R2,R2
        CMP  R5,#0  // if (S) // Not R0?
        BEQ  COM_S2a_F
        ADD  R5,R11
        LDRH R4,[R5,#0] // SRCE += (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R4,R4
        ADD  R2,R4
        UXTH R2,R2
COM_S2a_F:
        LSRS R5,R2,#1
        LSLS R5,#1 // SRCE &= 0xFFFE
        ADD  R5,R8
        MOV  PC,R7
.align 4
COM_S2a_II:
        ADD  R5,R11
        LDRH R2,[R5,#0] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R2,R2
        ADDS R4,R2,#2
        REV16 R4,R4
        STRH R4,[R5,#0]
        LSRS R5,R2,#1
        LSLS R5,#1 // SRCE &= 0xFFFE
        ADD  R5,R8
        MOV  PC,R7
.align 4
COM_S2a_I:
        ADD  R5,R11
        LDRH R5,[R5,#0] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R5,R5
        LSRS R5,#1
        LSLS R5,#1 // SRCE &= 0xFFFE
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
        LSRS R2,R0,#12
        LSLS R2,#2 // Got TS * 4, but with 2 extra random hi bits
        ADR  R4,COM_S2b
        LDR  R4,[R4,R2]
        MOV  PC,R4
.align 4
COM_S2b: .WORD COM_S2b_R, COM_S2b_I, COM_S2b_L, COM_S2b_II // random bits 00
         .WORD COM_S2b_R, COM_S2b_I, COM_S2b_L, COM_S2b_II // random bits 01
         .WORD COM_S2b_R, COM_S2b_I, COM_S2b_L, COM_S2b_II // random bits 10
         .WORD COM_S2b_R, COM_S2b_I, COM_S2b_L, COM_S2b_II // random bits 11
.align 4
COM_S2b_R:
        ADD  R5,R11 // SRCE = WP + S;
        B    COM_S2b_F2
.align 4
COM_S2b_L:
        MOVS  R2,R3
        ADDS R3,#2
        UXTH R3,R3
        ADD  R2,R8
        LDRH R2,[R2,#0] // SRCE = (memory [PC + 0] << 8) | memory [PC + 1];
        REV16 R2,R2
        CMP  R5,#0  // if (S) // Not R0?
        BEQ  COM_S2b_F
        ADD  R5,R11
        LDRH R4,[R5,#0] // SRCE += (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R4,R4
        ADD  R2,R4
        UXTH R2,R2
        B    COM_S2b_F
.align 4
COM_S2b_II:
        ADD  R5,R11
        LDRH R2,[R5,#0] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R2,R2
        ADDS R4,R2,#2
        REV16 R4,R4
        STRH R4,[R5,#0]
        B    COM_S2b_F
.align 4
COM_S2b_I:
        ADD  R5,R11
        LDRH R2,[R5,#0] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R2,R2
COM_S2b_F:
        LSRS R5,R2,#1
        LSLS R5,#1 // SRCE &= 0xFFFE
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
        UXTH R3,R3
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567 (Format 2)
//dddddddd0001oooo
.align 4
I_JMP:  SXTH R0,R0 // R0 now has a signed offset
        ASRS R0,#7 // Word offset
        ADDS R4,R0,#2 // Does this jump to itself?
        BNE  dojump   // Else quit the emulation
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
I_JLT:  SXTH R0,R0 // R0 now has a signed offset
        ASRS R0,#7 // Word offset
        MOVS R4,#0x60 // AG|EQ
        TST  R4,R1
        BNE  start  // At least one of them was not zero
        B    dojump
// *********************************************************************************************
.align 4
I_JLE:  SXTH R0,R0 // R0 now has a signed offset
        ASRS R0,#7 // Word offset
        LSRS R4,R1,#6 // EQ -> Carry
        BCS  dojump
        LSRS R4,R1,#8 // LG -> Carry
        BCS  start
        B    dojump
// *********************************************************************************************
.align 4
I_JEQ:  SXTH R0,R0 // R0 now has a signed offset
        ASRS R0,#7 // Word offset
        LSRS R4,R1,#6 // EQ -> Carry
        BCC  start
        B    dojump
// *********************************************************************************************
.align 4
I_JHE:  SXTH R0,R0 // R0 now has a signed offset
        ASRS R0,#7 // Word offset
        MOVS R4,#0xA0 // LG|EQ
        TST  R4,R1
        BEQ  start // Both were zero
        B    dojump
// *********************************************************************************************
.align 4
I_JGT:  SXTH R0,R0 // R0 now has a signed offset
        ASRS R0,#7 // Word offset
        LSRS R4,R1,#7 // AG -> Carry
        BCC  start
        B    dojump
// *********************************************************************************************
.align 4
I_JNE:  SXTH R0,R0 // R0 now has a signed offset
        ASRS R0,#7 // Word offset
        LSRS R4,R1,#6 // EQ -> Carry
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
dojump: ADDS R3,R0
        UXTH R3,R3 // Discard any extra
// *********************************************************************************************
start:  MOV  R0,R9
        LDRB R0,[R0,#0]
        LSRS R0,#1
        BCC  I_IDLE

        MOV  R0,R8
        LDRH R0,[R0,R3] // Get instruction BYTE|INST (LE format)
        ADDS R3,#2
        UXTH R3,R3

startX: REV16 R6,R0    // R6=INST|BYTE
        LSRS R2,R6,#5  // Get opcode
        LSLS R2,#3     // x8
        ADR  R4,JMPTBL // This is just in reach
        ADDS R4,R2
        LDM  R4,{R4,R7} // Get 1st and 2nd branch location (if it's needed)
        MOV  PC,R4

// *********************************************************************************************
.align 4
I_JNC:  SXTH R0,R0 // R0 now has a signed offset
        ASRS R0,#7 // Word offset
        LSRS R4,R1,#5 // C -> Carry
        BCS  start
        B    dojump
// *********************************************************************************************
.align 4
I_JOC:  SXTH R0,R0 // R0 now has a signed offset
        ASRS R0,#7 // Word offset
        LSRS R4,R1,#5 // C -> Carry
        BCC  start
        B    dojump
// *********************************************************************************************
.align 4
I_JNO:  SXTH R0,R0 // R0 now has a signed offset
        ASRS R0,#7 // Word offset
        LSRS R4,R1,#4 // O -> Carry
        BCS  start
        B    dojump
// *********************************************************************************************
.align 4
I_JL:   SXTH R0,R0 // R0 now has a signed offset
        ASRS R0,#7 // Word offset
        MOVS R4,#0xA0 // LG|EQ
        TST  R4,R1
        BNE  start // At least one of them was not zero
        B    dojump
// *********************************************************************************************
.align 4
I_JH:   SXTH R0,R0 // R0 now has a signed offset
        ASRS R0,#7 // Word offset
        LSRS R4,R1,#6 // EQ -> Carry
        BCS  start
        LSRS R4,R1,#8 // LG -> Carry
        BCC  start
        B    dojump
// *********************************************************************************************
.align 4
I_JOP:  SXTH R0,R0 // R0 now has a signed offset
        ASRS R0,#7 // Word offset
        LSRS R4,R1,#3 // P -> Carry
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
        LDR  R4,[R4,R2]
        MOV  PC,R4
.align 4
COM_S2d: .WORD COM_S2d_R, COM_S2d_I, COM_S2d_L, COM_S2d_II
.align 4
COM_S2d_R:
        ADD  R5,R11 // SRCE = WP + S;
        B    COM_S2d_F2
.align 4
COM_S2d_L:
        MOV  R2,R8
        LDRH R2,[R2,R3] // SRCE = (memory [PC + 0] << 8) | memory [PC + 1];
        ADDS R3,#2
        UXTH R3,R3
        REV16 R2,R2
        CMP  R5,#0  // if (S) // Not R0?
        BEQ  COM_S2d_F
        ADD  R5,R11
        LDRH R4,[R5,#0] // SRCE += (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R4,R4
        ADD  R2,R4
        UXTH R2,R2
        B    COM_S2d_F
.align 4
COM_S2d_II:
        ADD  R5,R11
        LDRH R2,[R5,#0] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R2,R2
        ADDS R4,R2,#2
        REV16 R4,R4
        STRH R4,[R5,#0]
        B    COM_S2d_F
.align 4
COM_S2d_I:
        ADD  R5,R11
        LDRH R2,[R5,#0] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R2,R2
COM_S2d_F:
        LSRS R5,R2,#1
        LSLS R5,#1 // SRCE &= 0xFFFE
        ADD  R5,R8
COM_S2d_F2:
// *********************************************************************************************
        LDRH R5,[R5,#0] // R5=SRCE - but note this is reversed byte order - instructions to fix.
// *********************************************************************************************
        LSLS R6,#22
        LSRS R6,#28
        LSLS R6,#1 // Got D << 1
        ADR  R4,COM_D2
        LDR  R4,[R4,R0]
        MOV  PC,R4
.align 4
COM_D2: .WORD COM_D2_R, COM_D2_I, COM_D2_L, COM_D2_II
.align 4
COM_D2_R:
        ADD  R6,R11 // DEST = WP + D;
        MOV  PC,R7
.align 4
COM_D2_L:
        MOV  R2,R8
        LDRH R2,[R2,R3] // DEST = (memory [PC + 0] << 8) | memory [PC + 1];
        ADDS R3,#2
        UXTH R3,R3
        REV16 R2,R2
        CMP  R6,#0  // if (D) // Not R0?
        BEQ  COM_D2_F
        ADD  R6,R11
        LDRH R4,[R6,#0] // DEST += (memory [WP + 0 + D] << 8) | memory [WP + 1 + D];
        REV16 R4,R4
        ADD  R2,R4
        UXTH R2,R2
COM_D2_F:
        LSRS R6,R2,#1
        LSLS R6,#1 // DEST &= 0xFFFE
        ADD  R6,R8
        MOV  PC,R7
.align 4
COM_D2_II:
        ADD  R6,R11
        LDRH R2,[R6,#0] // DEST = (memory [WP + 0 + D] << 8) | memory [WP + 1 + D];
        REV16 R2,R2
        ADDS R4,R2,#2
        REV16 R4,R4
        STRH R4,[R6,#0]
        LSRS R6,R2,#1
        LSLS R6,#1 // DEST &= 0xFFFE
        ADD  R6,R8
        MOV  PC,R7
.align 4
COM_D2_I:
        ADD  R6,R11
        LDRH R6,[R6,#0] // DEST = (memory [WP + 0 + D] << 8) | memory [WP + 1 + D];
        REV16 R6,R6
        LSRS R6,#1
        LSLS R6,#1 // DEST &= 0xFFFE
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
        LDR  R4,[R4,R2]
        MOV  PC,R4
.align 4
COM_S1: .WORD COM_S1_R, COM_S1_I, COM_S1_L, COM_S1_II
.align 4
COM_S1_R:
        ADD  R5,R11 // SRCE = WP + S;
        LDRB R5,[R5,#0] // R5=SRCE
        B    COM_S1_F2
.align 4
COM_S1_L:
        MOV  R2,R8
        LDRH R2,[R2,R3] // SRCE = (memory [PC + 0] << 8) | memory [PC + 1];
        ADDS R3,#2
        UXTH R3,R3
        REV16 R2,R2
        CMP  R5,#0  // if (S) // Not R0?
        BEQ  COM_S1_F
        ADD  R5,R11
        LDRH R4,[R5,#0] // SRCE += (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R4,R4
        ADD  R2,R4
        UXTH R2,R2
        B    COM_S1_F
.align 4
COM_S1_II:
        ADD  R5,R11
        LDRH R2,[R5,#0] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
        REV16 R2,R2
        ADDS R4,R2,#1
        REV16 R4,R4
        STRH R4,[R5,#0]
        B    COM_S1_F
.align 4
COM_S1_I:
        ADD  R5,R11
        LDRH R2,[R5,#0] // SRCE = (memory [WP + 0 + S] << 8) | memory [WP + 1 + S];
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
        LDR  R4,[R4,R0]
        MOV  PC,R4
.align 4
COM_D1: .WORD COM_D1_R, COM_D1_I, COM_D1_L, COM_D1_II
.align 4
COM_D1_R:
        ADD  R6,R11 // DEST = WP + D;
        MOV  PC,R7
.align 4
COM_D1_L:
        MOV  R2,R8
        LDRH R2,[R2,R3] // DEST = (memory [PC + 0] << 8) | memory [PC + 1];
        ADDS R3,#2
        UXTH R3,R3
        REV16 R2,R2
        CMP  R6,#0  // if (D) // Not R0?
        BEQ  COM_D1_F
        ADD  R6,R11
        LDRH R4,[R6,#0] // DEST += (memory [WP + 0 + D] << 8) | memory [WP + 1 + D];
        REV16 R4,R4
        ADD  R2,R4
        UXTH R6,R2
        ADD  R6,R8
        MOV  PC,R7
.align 4
COM_D1_F:
        MOVS R6,R2
        ADD  R6,R8
        MOV  PC,R7
.align 4
COM_D1_II:
        ADD  R6,R11
        LDRH R2,[R6,#0] // DEST = (memory [WP + 0 + D] << 8) | memory [WP + 1 + D];
        REV16 R2,R2
        ADDS R4,R2,#1
        REV16 R4,R4
        STRH R4,[R6,#0]
        MOVS R6,R2
        ADD  R6,R8
        MOV  PC,R7
.align 4
COM_D1_I:
        ADD  R6,R11
        LDRH R6,[R6,#0] // DEST = (memory [WP + 0 + D] << 8) | memory [WP + 1 + D];
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
        ADDS R2,R4      // R2=000yyyyy xxxxxzzz
        MOVS R7,#7
        ANDS R4,R7      // R4=00000000 00000xxx (bit offset)
        SUBS R2,R4      // R2=000yyyyy xxxxxyyy (byte offset)

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
        LSRS R4,#1
        LSLS R4,#1 // R4 &= 0xFFFE

        ADDS R4,#2
        UXTH R4,R4
        REV16 R2,R4
        STRH R2,[R0,#30] // R15
        ADD  R4,R8

        LDRH R3,[R4,#0] // Get the value off the stack
        REV16 R3,R3
        LSRS R3,#1
        LSLS R3,#1 // R3 &= 0xFFFE
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567 (Format 6)
//10TsSSSS00001100
.align 4
I_CALL: MOV  R0,R11 // WP
        LDRH R4,[R0,#30] // Get R15
        REV16 R4,R4
        LSRS R4,#1
        LSLS R4,#1 // R4 &= 0xFFFE

        SUBS R2,R4,#2 // Stack word has been consumed
        REV16 R2,R2
        STRH R2,[R0,#30] // Store new R15
        ADD  R4,R8

        REV16 R3,R3
        STRH R3,[R4,#0] // Old *R15

        MOV  R0,R8
        SUBS R3,R5,R0 // Normalise
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
        LSRS R4,#1
        LSLS R4,#1 // R4 &= 0xFFFE

        SUBS R2,R4,#2 // Stack word has been consumed
        REV16 R2,R2
        STRH R2,[R0,#30] // Store new R15
        ADD  R4,R8

        STRH R5,[R4,#0] // Old *R15
        B    start
// *********************************************************************************************
//R0 (LE)
//89ABCDEF01234567 (Format 6)
//00TsSSSS00001111
.align 4
I_POP:  MOV  R0,R11 // WP
        LDRH R4,[R0,#30] // Get R15
        REV16 R4,R4
        LSRS R4,#1
        LSLS R4,#1 // R4 &= 0xFFFE

        ADDS R4,#2 // Stack word has been consumed
        UXTH R4,R4
        REV16 R2,R4
        STRH R2,[R0,#30] // Store new R15
        ADD  R4,R8

        LDRH R0,[R4,#0] // *R15
        STRH R0,[R5,#0] // R5=DEST
        B    start
// *********************************************************************************************
.align 4
JMPTBL: .WORD start    , 0     , start    , 0     , start    , 0     , start    , 0     , start    , 0     , start    , 0     , start    , 0     , start    , 0        // >0000
        .WORD start    , 0     , start    , 0     , start    , 0     , start    , 0     , start    , 0     , start    , 0     , start    , 0     , start    , 0        // >0100
        .WORD I_LI     , 0     , I_AI     , 0     , I_ANDI   , 0     , I_ORI    , 0     , I_CI     , 0     , I_STWP   , 0     , I_STST   , 0     , I_LWPI   , 0        // >0200
        .WORD I_LIMI   , 0     , start    , 0     , I_IDLE   , 0     , start    , 0     , I_RTWP   , 0     , start    , 0     , start    , 0     , start    , 0        // >0300
        .WORD sdecode2a, I_BLWP, sdecode2a, I_BLWP, sdecode2a, I_B   , sdecode2a, I_B   , sdecode2a, I_X   , sdecode2a, I_X   , sdecode2a, I_CLR , sdecode2a, I_CLR    // >0400
        .WORD sdecode2a, I_NEG , sdecode2a, I_NEG , sdecode2a, I_INV , sdecode2a, I_INV , sdecode2a, I_INC , sdecode2a, I_INC , sdecode2a, I_INCT, sdecode2a, I_INCT   // >0500
        .WORD sdecode2a, I_DEC , sdecode2a, I_DEC , sdecode2a, I_DECT, sdecode2a, I_DECT, sdecode2a, I_BL  , sdecode2a, I_BL  , sdecode2a, I_SWPB, sdecode2a, I_SWPB   // >0600
        .WORD sdecode2a, I_SETO, sdecode2a, I_SETO, sdecode2a, I_ABS , sdecode2a, I_ABS , start    , 0     , start    , 0     , start    , 0     , start    , 0        // >0700
        .WORD I_SRA    , 0     , I_SRA    , 0     , I_SRA    , 0     , I_SRA    , 0     , I_SRA    , 0     , I_SRA    , 0     , I_SRA    , 0     , I_SRA    , 0        // >0800
        .WORD I_SRL    , 0     , I_SRL    , 0     , I_SRL    , 0     , I_SRL    , 0     , I_SRL    , 0     , I_SRL    , 0     , I_SRL    , 0     , I_SRL    , 0        // >0900
        .WORD I_SLA    , 0     , I_SLA    , 0     , I_SLA    , 0     , I_SLA    , 0     , I_SLA    , 0     , I_SLA    , 0     , I_SLA    , 0     , I_SLA    , 0        // >0A00
        .WORD I_SRC    , 0     , I_SRC    , 0     , I_SRC    , 0     , I_SRC    , 0     , I_SRC    , 0     , I_SRC    , 0     , I_SRC    , 0     , I_SRC    , 0        // >0B00
        .WORD I_RET    , 0     , start    , 0     , start    , 0     , start    , 0     , sdecode2a, I_CALL, sdecode2a, I_CALL, start    , 0     , start    , 0        // >0C00
        .WORD sdecode2a, I_PUSH, sdecode2a, I_PUSH, start    , 0     , start    , 0     , start    , 0     , start    , 0     , start    , 0     , start    , 0        // >0D00
        .WORD I_SLC    , 0     , I_SLC    , 0     , start    , 0     , start    , 0     , start    , 0     , start    , 0     , start    , 0     , start    , 0        // >0E00
        .WORD sdecode2a, I_POP , sdecode2a, I_POP , start    , 0     , start    , 0     , start    , 0     , start    , 0     , start    , 0     , start    , 0        // >0F00
        .WORD I_JMP    , 0     , I_JMP    , 0     , I_JMP    , 0     , I_JMP    , 0     , I_JMP    , 0     , I_JMP    , 0     , I_JMP    , 0     , I_JMP    , 0        // >1000
        .WORD I_JLT    , 0     , I_JLT    , 0     , I_JLT    , 0     , I_JLT    , 0     , I_JLT    , 0     , I_JLT    , 0     , I_JLT    , 0     , I_JLT    , 0        // >1100
        .WORD I_JLE    , 0     , I_JLE    , 0     , I_JLE    , 0     , I_JLE    , 0     , I_JLE    , 0     , I_JLE    , 0     , I_JLE    , 0     , I_JLE    , 0        // >1200
        .WORD I_JEQ    , 0     , I_JEQ    , 0     , I_JEQ    , 0     , I_JEQ    , 0     , I_JEQ    , 0     , I_JEQ    , 0     , I_JEQ    , 0     , I_JEQ    , 0        // >1300
        .WORD I_JHE    , 0     , I_JHE    , 0     , I_JHE    , 0     , I_JHE    , 0     , I_JHE    , 0     , I_JHE    , 0     , I_JHE    , 0     , I_JHE    , 0        // >1400
        .WORD I_JGT    , 0     , I_JGT    , 0     , I_JGT    , 0     , I_JGT    , 0     , I_JGT    , 0     , I_JGT    , 0     , I_JGT    , 0     , I_JGT    , 0        // >1500
        .WORD I_JNE    , 0     , I_JNE    , 0     , I_JNE    , 0     , I_JNE    , 0     , I_JNE    , 0     , I_JNE    , 0     , I_JNE    , 0     , I_JNE    , 0        // >1600
        .WORD I_JNC    , 0     , I_JNC    , 0     , I_JNC    , 0     , I_JNC    , 0     , I_JNC    , 0     , I_JNC    , 0     , I_JNC    , 0     , I_JNC    , 0        // >1700
        .WORD I_JOC    , 0     , I_JOC    , 0     , I_JOC    , 0     , I_JOC    , 0     , I_JOC    , 0     , I_JOC    , 0     , I_JOC    , 0     , I_JOC    , 0        // >1800
        .WORD I_JNO    , 0     , I_JNO    , 0     , I_JNO    , 0     , I_JNO    , 0     , I_JNO    , 0     , I_JNO    , 0     , I_JNO    , 0     , I_JNO    , 0        // >1900
        .WORD I_JL     , 0     , I_JL     , 0     , I_JL     , 0     , I_JL     , 0     , I_JL     , 0     , I_JL     , 0     , I_JL     , 0     , I_JL     , 0        // >1A00
        .WORD I_JH     , 0     , I_JH     , 0     , I_JH     , 0     , I_JH     , 0     , I_JH     , 0     , I_JH     , 0     , I_JH     , 0     , I_JH     , 0        // >1B00
        .WORD I_JOP    , 0     , I_JOP    , 0     , I_JOP    , 0     , I_JOP    , 0     , I_JOP    , 0     , I_JOP    , 0     , I_JOP    , 0     , I_JOP    , 0        // >1C00
        .WORD I_SBO    , 0     , I_SBO    , 0     , I_SBO    , 0     , I_SBO    , 0     , I_SBO    , 0     , I_SBO    , 0     , I_SBO    , 0     , I_SBO    , 0        // >1D00
        .WORD I_SBZ    , 0     , I_SBZ    , 0     , I_SBZ    , 0     , I_SBZ    , 0     , I_SBZ    , 0     , I_SBZ    , 0     , I_SBZ    , 0     , I_SBZ    , 0        // >1E00
        .WORD I_TB     , 0     , I_TB     , 0     , I_TB     , 0     , I_TB     , 0     , I_TB     , 0     , I_TB     , 0     , I_TB     , 0     , I_TB     , 0        // >1F00
        .WORD sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC    // >2000
        .WORD sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC    // >2100
        .WORD sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC    // >2200
        .WORD sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC , sdecode2b, I_COC    // >2300
        .WORD sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC    // >2400
        .WORD sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC    // >2500
        .WORD sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC    // >2600
        .WORD sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC , sdecode2b, I_CZC    // >2700
        .WORD sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR    // >2800
        .WORD sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR    // >2900
        .WORD sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR    // >2A00
        .WORD sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR , sdecode2b, I_XOR    // >2B00
        .WORD sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP    // >2C00
        .WORD sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP    // >2D00
        .WORD sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP    // >2E00
        .WORD sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP , sdecode2b, I_XOP    // >2F00
        .WORD start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0        // >3000
        .WORD start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0        // >3100
        .WORD start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0        // >3200
        .WORD start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0        // >3300
        .WORD start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0        // >3400
        .WORD start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0        // >3500
        .WORD start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0        // >3600
        .WORD start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0     , start    , 0     , OP_3LO   , 0        // >3700
        .WORD sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL    // >3800
        .WORD sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL    // >3900
        .WORD sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL    // >3A00
        .WORD sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL , sdecode2b, I_MUL    // >3B00
        .WORD sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV    // >3C00
        .WORD sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV    // >3D00
        .WORD sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV    // >3E00
        .WORD sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV , sdecode2b, I_DIV    // >3F00
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4000
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4100
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4200
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4300
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4400
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4500
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4600
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4700
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4800
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4900
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4A00
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4B00
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4C00
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4D00
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4E00
        .WORD sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC , sdecode2d, I_SZC    // >4F00
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5000
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5100
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5200
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5300
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5400
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5500
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5600
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5700
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5800
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5900
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5A00
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5B00
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5C00
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5D00
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5E00
        .WORD sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB, sdecode1d, I_SZCB   // >5F00
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6000
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6100
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6200
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6300
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6400
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6500
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6600
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6700
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6800
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6900
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6A00
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6B00
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6C00
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6D00
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6E00
        .WORD sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S   , sdecode2d, I_S      // >6F00
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7000
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7100
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7200
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7300
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7400
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7500
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7600
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7700
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7800
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7900
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7A00
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7B00
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7C00
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7D00
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7E00
        .WORD sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB  , sdecode1d, I_SB     // >7F00
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8000
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8100
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8200
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8300
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8400
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8500
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8600
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8700
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8800
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8900
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8A00
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8B00
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8C00
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8D00
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8E00
        .WORD sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C   , sdecode2d, I_C      // >8F00
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9000
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9100
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9200
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9300
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9400
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9500
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9600
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9700
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9800
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9900
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9A00
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9B00
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9C00
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9D00
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9E00
        .WORD sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB  , sdecode1d, I_CB     // >9F00
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >A000
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >A100
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >A200
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >A300
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >A400
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >A500
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >A600
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >A700
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >A800
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >A900
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >AA00
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >AB00
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >AC00
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >AD00
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >AE00
        .WORD sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A   , sdecode2d, I_A      // >AF00
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >B000
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >B100
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >B200
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >B300
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >B400
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >B500
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >B600
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >B700
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >B800
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >B900
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >BA00
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >BB00
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >BC00
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >BD00
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >BE00
        .WORD sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB  , sdecode1d, I_AB     // >BF00
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >C000
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >C100
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >C200
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >C300
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >C400
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >C500
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >C600
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >C700
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >C800
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >C900
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >CA00
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >CB00
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >CC00
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >CD00
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >CE00
        .WORD sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV , sdecode2d, I_MOV    // >CF00
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >D000
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >D100
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >D200
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >D300
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >D400
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >D500
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >D600
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >D700
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >D800
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >D900
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >DA00
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >DB00
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >DC00
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >DD00
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >DE00
        .WORD sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB, sdecode1d, I_MOVB   // >DF00
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >E000
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >E100
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >E200
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >E300
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >E400
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >E500
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >E600
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >E700
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >E800
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >E900
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >EA00
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >EB00
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >EC00
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >ED00
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >EE00
        .WORD sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC , sdecode2d, I_SOC    // >EF00
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >F000
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >F100
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >F200
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >F300
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >F400
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >F500
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >F600
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >F700
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >F800
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >F900
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >FA00
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >FB00
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >FC00
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >FD00
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >FE00
        .WORD sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB, sdecode1d, I_SOCB   // >FF00

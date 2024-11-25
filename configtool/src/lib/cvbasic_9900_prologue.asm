;
; CVBasic prologue (BASIC compiler, 9900 target)
;
; by Tursi
; https//harmlesslion.com
;
; based on code by
;
; by Oscar Toledo G.
; https//nanochess.org/
;
; Creation date Aug/05/2024.
; Revision date Aug/06/2024. Ported music player from Z80 CVBasic.
; Revision date Aug/07/2024. Ported Pletter decompressor from Z80 CVBasic.
;                            Added VDP delays.
; Revision date Aug/12/2024. Rewrite started for TMS9900 cpu
; Revision date Aug/16/2024. Corrected bug in define_char_unpack.
; Revision date Aug/18/2024. Ported bugfixes to TMS9900 version
; Revision date Aug/30/2024. All samples except pletter and banking working on TMS9900 version
; Revision date Oct/15/2024. Added LDIRMV.

;
; Platforms supported:
; o TI-99/4A with 32k Memory Expansion

; this is intended to be assembled by xdt99, no console ROM dependencies
;   cvbasic --ti994a test1.bas test1.a99
;   xas99.py -R test1.a99

; When looking at this - remember that JUMP and BRANCH have the /opposite/
; meanings to the 6502 - JUMP is the short relative one, and BRANCH is absolute.

;
; CVBasic variables in scratchpad.
;

; don't warn on branch/jump optimizations or unused variables
;: warn-opts = off
;: warn-symbols = off

; use original (possibly incorrectly ported) random
OLD_RND equ 0

; We have to use our own workspace, not GPLWS, because the interrupt routine makes it unsafe to
; use r11, which we kind of need! So that eats 32 bytes of RAM but means most of the register
; restrictions I wrote this code on (avoid R1, avoid R6, avoid R13-R15) are lifted.
mywp      equ >8300
myintwp   equ >8320

; data storage in scratchpad
    dorg >8340

; used to track scratchpad variables
firstsp         equ $

read_pointer    bss 2       ; for data/read statements
cursor		    bss 2       ; screen position
pletter_off	    bss 2       ; Used by Pletter

; joystick bytes
joy1_data	    bss 1       ; keep these bytes together and even aligned
joy2_data	    bss 1

key1_data	    bss 1       ; byte - keyboard - keep these bytes together and even aligned
key2_data	    bss 1       ; byte - keyboard (not used)

frame	        bss 2       ; word
lfsr		    bss 2       ; word MUST BE EVEN ALIGNED

mode            bss 1
flicker         bss 1

ntsc            bss 1

    .ifne CVBASIC_MUSIC_PLAYER
music_playing		bss 1

music_start		    bss 2   ; word MUST BE EVEN ALIGNED
music_pointer		bss 2   ; word MUST BE EVEN ALIGNED

audio_freq1		    bss 2   ; word MUST BE EVEN ALIGNED
audio_freq2		    bss 2   ; word MUST BE EVEN ALIGNED
audio_freq3		    bss 2   ; word MUST BE EVEN ALIGNED

music_timing		bss 1       
music_note_counter	bss 1
music_instrument_1	bss 1

music_note_1		bss 1
music_counter_1	    bss 1

music_instrument_2	bss 1
music_note_2		bss 1

music_counter_2	    bss 1
music_instrument_3	bss 1

music_note_3		bss 1
music_counter_3	    bss 1

music_drum		    bss 1
music_counter_4	    bss 1

audio_vol1  		bss 1
audio_vol2	    	bss 1
audio_vol3		    bss 1
audio_vol4hw		bss 1

audio_noise 		bss 1
audio_control		bss 1

music_mode	    	bss 1
music_frame         bss 1
    .endif
    
    .ifne CVBASIC_BANK_SWITCHING
    even
saved_bank          bss 2
music_bank          bss 2
    .endif

; used to track scratchpad variables
    even
lastsp              equ $

; While we don't mean to USE the console ROM, for interrupts we
; are forced to interface with some of it. We need these addresses
; to minimize what it does so we can maximize our use of scratchpad.
; While I'd like to use the cassette hook - requires only 10 instructions
; and only uses 6 words of scratchpad, we can't here because it
; loses the return address, meaning you can only use it if you
; know where your LIMI 2 is and interrupts are otherwise disabled. So
; we have to use the longer but more standard interrupt hook, which also
; reads VDP status for us (no choice).

intcnt              equ >8379   ; interrupt counter byte, adds 1 (from GPLWS r14) every frame
vdp_status          equ >837B   ; VDP status byte mirror
intwsr1             equ >83c2   ; INT WS R1  - interrupt control flags - must be >8000
intwsr2             equ >83c4   ; INT WS R2  - address of user interrupt routine (point to int_handler)
intwsr11            equ >83d6   ; screen timeout counter - must be odd (init to 1, is inct every frame)
intwsr13            equ >83da   ; INT WS R13 - used for interrupt call (word)
intwsr14            equ >83dc   ; INT WS R14 - used for interrupt call (word)
intwsr15            equ >83de   ; INT WS R15 - used for interrupt call (word)
gplwsr11            equ >83f6   ; GPL WS R11 - return address to interrupt ROM (not used, but overwritten each int)
gplwsr12            equ >83f8   ; GPL WS R12 - used for cassette test and interrupt hook test (zeroed each int)
gplwsr13            equ >83fa   ; GPL WS R13 - used in my interrupt handler
gplwsr14            equ >83fc   ; GPL WS R14 - flags used to detect cassette - must be >0108 (or at least >0020 clear)
gplwsr15            equ >83fe   ; GPL WS R15 - base address of VDP for status read - must be >8C02

; Some hardware equates
INTWS     equ >83C0     ; interrupt calling Workspace
GPLWP     equ >83E0     ; we use this one
SOUND     equ >8400
VDPDATA   equ >8800
VDPSTATUS equ >8802
VDPWDATA  equ >8c00
VDPWADR   equ >8c02

; cartridge header for all ROM pages
; this might do weird things to bank 0 but we have to chop it up anyway...
    bank all,>6000
    
    data >aa01,>0100,>0000,proglist,>0000,>0000
proglist
    data >0000,SFIRST
    byte 20
    text 'CVBASIC GAME        *'    ; 20 characters to allow name to be hex edited
    
; startup code copies the first three banks to 24k RAM (always) and jumps there    
SFIRST
    clr @>6000      ; set bank 0 - last shared instruction

    bank 0

    li r3,3         ; how many banks
    li r4,>6000     ; from bank
    li r0,>a000     ; target in RAM
SFLP
    clr *r4+        ; set the bank
    li r1,>6050     ; from address
    li r2,>1FB0     ; count
SFLP2
    mov *r1+,*r0+   ; move words
    dect r2         ; count down
    jne SFLP2
    dec r3          ; count down pages
    jne SFLP
CODEST
    b @START        ; jump to startup code in RAM

; fixed program in high RAM - magic bank number higher than normally legal
; this will be chopped up and inserted into the first three banks
; we're still in bank 0 - this will result in a gap in the output binary
; that we can use to put the pieces together more easily.
    aorg >a000

; Utility functions

; Write register to VDP - R0 = reg in MSB, data in LSB
WRTVDP
    ori r0,>8000
    jmp SETRD

; Set VDP for write address - address in R0
SETWRT
    ori r0,>4000
; fall through

; Set VDP for read address - address in R0
SETRD
    swpb r0
    movb r0,@VDPWADR
    swpb r0
    movb r0,@VDPWADR
    b *r11

; Write byte to VDP - address in R0, data in MSB R2
; Inline address set to avoid needing to cache r11
WRTVRM
    ori r0,>4000
    swpb r0
    movb r0,@VDPWADR
    swpb r0
    movb r0,@VDPWADR
; No need to delay after setting a write address - there's no VRAM access
    movb r2,@VDPWDATA
    b *r11

; Read byte from VDP - address in R0, data returned in MSB R0
; Inline address set to avoid needing to cache r11
RDVRM
    swpb r0
    movb r0,@VDPWADR
    swpb r0
    movb r0,@VDPWADR
    nop
    movb @VDPDATA,r0
    b *r11

; Read the status register from VDP - data returned in LSB R0 (visrealm)
RDVST
    movb @VDPSTATUS,r0
    srl r0,8
	b *r11

; Fill VRAM - address in R0, byte in R2, count in R3
; Original: address in pointer, byte in temp, count in temp2 (ZP)
; Inline address set to avoid needing to cache r11
FILVRM
    ori r0,>4000
    swpb r0
    movb r0,@VDPWADR
    swpb r0
    movb r0,@VDPWADR
; No need to delay after setting a write address - there's no VRAM access
!1
    movb r2,@VDPWDATA
    dec r3
    jne -!1
    b *r11

; Read VRAM - address in R0, CPU data at R2, count in R3
; Inline address set to avoid needing to cache r11
LDIRMV
    swpb r2
    movb r2,@VDPWADR
    swpb r2
    movb r2,@VDPWADR
    swpb r2
    swpb r2
!1
    movb @VDPDATA,*r0+
    dec r3
    jne -!1
    b *r11

; Load VRAM - address in R0, CPU data at R2, count in R3
; Original: address in pointer, CPU address at temp, count in temp2
; Inline address set to avoid needing to cache r11
LDIRVM
    ori r0,>4000
    swpb r0
    movb r0,@VDPWADR
    swpb r0
    movb r0,@VDPWADR
; No need to delay after setting a write address - there's no VRAM access
!1
    movb *r2+,@VDPWDATA
    dec r3
    jne -!1
    b *r11

; Define a pattern three times with 2k offsets - used for bitmap color and pattern tables
; Load VRAM 3 times with offset - address in R0, CPU data at R2, count in R3
; Original: address in pointer, CPU address at temp, count in temp2
LDIRVM3
    mov r11,r4      ; save return address
    mov r2,r5       ; save CPU address
    mov r3,r7       ; save count
    bl @LDIRVM
    ai r0,>0800     ; the OR'd mask doesn't matter
    mov r5,r2       ; restore CPU
    mov r7,r3       ; restore count
    bl @LDIRVM
    ai r0,>0800     ; the OR'd mask doesn't matter
    mov r5,r2
    mov r7,r3
    mov r4,r11      ; for tail recursion
    jmp LDIRVM

; Disable screen by setting VDP register 1 to >a2
DISSCR
    limi 0
    li r0,>81a2
DISSCR2
    swpb r0
    movb r0,@VDPWADR
    swpb r0
    movb r0,@VDPWADR
    limi 2
    b *r11

; enable screen by setting VDP register 1 to >E2
ENASCR
    limi 0
    li r0,>81e2
    jmp DISSCR2

; copy a set of blocks of data to VDP, offset by 32 bytes each
; address in R8, CPU data at R9, count per row in R6 (MSB), number rows in R4 (MSB), CPU stride in R5 (MSB) (VDP stride fixed at 32)
; original: address in pointer, CPU address at temp, count per row in temp2, num rows in temp2+1, stride in YYXX
CPYBLK
    limi 0
    mov r11,r7      ; save return
    srl r6,8
    srl r4,8
    srl r5,8        ; bytes to words
!1
    mov r8,r0       ; get vdp address
    mov r9,r2       ; get cpu address
    mov r6,r3       ; get count
!2
    bl @LDIRVM      ; copy one row
    a r5,r9         ; add stride to CPU address
    ai r8,32        ; add 32 to VDP
    dec r4          ; count down rows
    jne -!1         ; loop till done
    limi 2
    b *r7           ; back to caller

; clear screen and reset cursor to >1800
cls
    mov r11,r4      ; save return
    li r0,>1800     ; SIT address
    mov r0,@cursor  ; save cursor
    li r2,>2000     ; byte to write
    li r3,768       ; number of bytes
    limi 0          ; ints off
    bl @FILVRM      ; write them
    limi 2          ; ints back on
    b *r4           ; back to caller

; copy a string to screen at cursor - address enforced
; CPU address in R2, length in R3
print_string
    mov r11,r4      ; save return
    mov @cursor,r0  ; get cursor pos
    andi r0,>07ff   ; enforce position - pretty large range though? 80 column support maybe?
    ai r0,>1800     ; add is safer than OR, and we have that option
    a r3,@cursor    ; add the count to cursor (might as well do it now!)
    limi 0
    bl @LDIRVM      ; do the write
    limi 2
    b *r4           ; back to caller

; emit a 16-bit number as decimal with leading zero masking at cursor
; R0 - number to print
; original number in YYAA?
print_number
    limi 0              ; interrupts off so we can hold the VDP address
    clr r5              ; leading zero flag
print_number5
    li r1,10000         ; divisor
    mov r11,r4
    bl @print_digit
    mov r4,r11
print_number4
    li r1,1000          ; divisor
    mov r11,r4
    bl @print_digit
    mov r4,r11
print_number3
    li r1,100           ; divisor
    mov r11,r4
    bl @print_digit
    mov r4,r11
print_number2
    li r1,10
    mov r11,r4
    bl @print_digit
    mov r4,r11
print_number1
    li r1,1
    andi r5,>00ff
    ori r5,>0100
    mov r11,r4
    bl @print_digit
    limi 2              ; ints on
    b *r4               ; back to caller

print_digit
    mov r11,r6
    clr r2
    div r1,r2
    ai r2,>30
    ci r2,>30
    jne !3
    ci r5,>0100
    jhe !4
    b *r6
!4
    ci r5,>0200
    jl !6
    mov r5,r2
    jne !5
!6
    li r2,>30
!3
    andi r5,>00ff
    ori r5,>0100
!5
    mov @cursor,r0      ; get cursor
    andi r0,>07ff       ; enforce position - large range for two screen pages
    ai r0,>1800         ; add is safer than OR, and we have that option
    bl @SETWRT          ; set write address
    swpb r2
    movb r2,@VDPWDATA
    inc @cursor         ; track it
    b *r6

; Load sprite definitions: Sprite number in R4, CPU data in R0, count of sprites in R5 (MSB)
; Original: pointer = sprite number, temp = CPU address, a = number sprites
; Note: sprites are all expected to be double-size 16x16, 32 bytes each, so sprite char 1 is character 4
; Sprite pattern table at >3800
define_sprite
    mov r11,r8          ; save return
    mov r0,r2           ; Source data.
    mov r4,r0           ; VRAM target.
    sla r0,5            ; sprite number times 32 for bytes
    ai r0,>3800         ; add VDP base
    mov r5,r3		; Length.
    srl r3,8            ; make int
    sla r3,5            ; count times 32
    limi 0              ; ints off
    bl @LDIRVM          ; do the copy
    limi 2              ; ints on
    b *r8               ; back to caller

; Load character definitions: Char number in R1, CPU data in R2, count in R3 (MSB)
; Original: pointer = char number, temp = CPU address, a = number chars
; Note this loads the pattern three times if in bitmap mode (MODE&0x04)
; Pattern table at >0000
define_char
    mov r11,r8          ; save return
    mov r0,r2           ; source data
    mov r4,r0           ; move input to scratch
    sla r0,3            ; char number times 8 (VDP base is 0, so already there)
    mov r5,r3
    srl r3,8            ; make word
    sla r3,3            ; count times 8
    movb @mode,r5       ; get mode flags
    andi r5,>0800
    jne !1              ; not in bitmap mode, do a single copy

    limi 0              ; ints off
    bl @LDIRVM3         ; do the triple copy
    limi 2              ; ints on
    b *r8               ; back to caller

!1
    limi 0              ; ints off
    bl @LDIRVM          ; do the single copy
    limi 2              ; ints on
    b *r8               ; back to caller

; Load bitmap color definitions: Char number in R1, CPU data in R2, count in R3 (MSB)
; Original: pointer = char number, temp = CPU address, a = number chars
; Note: always does the triple copy. Color table at >2000
define_color
    mov r11,r8          ; save return
    mov r0,r2           ; source data
    mov r4,r0           ; move input to scratch
    sla r0,3            ; char number times 8
    ai r0,>2000         ; add base address
    mov r5,r3
    srl r3,8            ; make word
    sla r3,3            ; count times 8
    limi 0              ; ints off
    bl @LDIRVM3         ; do the triple copy
    limi 2              ; ints on
    b *r8               ; back to caller

; Update sprite entry - copy data (4 bytes) to sprite table mirror at sprites
; R4 = sprite number, R5 = byte 1, r6 = byte 2, r7 = byte 3, r0 = byte 4 (all MSB)
; Original: A = sprite number, source data at sprite_data
update_sprite
    srl r4,8            ; make word
    sla r4,2            ; x4 for address
    ai r4,sprites       ; sprite mirror address
    movb r5,*r4+        ; move bytes
    movb r6,*r4+        ; move bytes
    movb r7,*r4+        ; move bytes
    movb r0,*r4+        ; move bytes
    b *r11

; SGN R0 - return 1, -1 or 0 as 16 bit
_sgn16
    mov r0,r0       ; check for zero
    jeq !1          ; if yes, we're done
    andi r0,>8000   ; check for negative
    jeq !2          ; was not
    seto r0         ; was negative, make it -1
    b *r11          ; back to caller
!2
    inc r0          ; we know it was zero, and we want 1
!1
    b *r11          ; back to caller

; 16-bit signed modulo. R1 % R2 = R0 - 9900 doesn't do signed divide
; original was stack%stack=YYAA
; Remainder is negative if the dividend was negative
_mod16s
    clr r0          ; make dividend 32-bit
    mov r2,r2       ; check divisor for zero
    jeq !1
    abs r2          ; make sure divisor is positive
    mov r1,r1       ; check sign of dividend
    jgt !2          ; go do the faster positive version

    abs r1          ; was negative, make it positive
    div r2,r0       ; do the division => r2=quotient, r3=remainder
    neg r1          ; make remainder negative
    mov r1,r0       ; into r0
    b *r11

!2
    div r2,r0       ; do the division => r2=quotient, r3=remainder
    mov r1,r0       ; into r0
!1
    b *r11

; 16-bit signed divide. R1 / R2 = R0 - 9900 doesn't do signed divide
; original was stack/stack=YYAA
; Remainder is negative if the signs differ
_div16s
    clr r0          ; make dividend 32-bit
    mov r2,r3       ; check divisor for zero
    jeq !1          ; 
    xor r1,r3
    abs r2
    abs r1          ; might as well make them positive now that we have copies
    div r2,r0       ; do the divide => r0=quotient, r1=remainder
    andi r3,>8000   ; mask out sign bit
    jeq !1          ; skip ahead to positive version

    neg r0          ; negate the result
!1
    b *r11

; Random number generator - return in R0, (complex one uses R3,R4, simpler one only R0)
; Original output into YYAA
random
    .ifne OLD_RND
; TODO: Not 100% sure I ported this one right... probably could be simpler with 16-bit manips...
    mov @lfsr,r0        ; fetch current state
    jne !0
    li r0,>7811         ; reset value if zero
    mov r0,@lfsr
!0
    movb @lfsr+1,r0
    movb @mywp,@mywp+1  ; trick, copy msb to lsb (so the 16-bit rotate works)
    mov r0,r3           ; we use this again
    src r0,2            ; circular rotate twice (rotates directly like z80)
    xor @lfsr+1,r0      ; because of 16 bit addressing, only the LSB is correct
    movb @mywp+1,@mywp  ; fix up - copy LSB to MSB
    mov r0,r4           ; save it (temp)
    src r3,1            ; rotate the second read once
    xor r3,r4           ; xor into the temp copy
    movb @lfsr,r0       ; get the lsb
    sla r0,2            ; just a straight shift
    xor r4,r0           ; xor the temp copy in (both bytes of r4 were valid)
    mov @lfsr,r4        ; get word for shifting
    srl r4,1            ; shift once
    socb r0,r4          ; merge in the msb we just generated
    mov r4,@lfsr        ; write it back
    mov r4,r0           ; for return
    b *r11
    .else
; simpler one from dreamcast days...
    mov @lfsr,r0        ; get seed
    srl r0,1            ; shift
    jnc .rand1          ; jump if no 1
    xor @rmask,r0       ; xor new bits
.rand1
    mov r0,@lfsr        ; save the output
    b *r11
rmask
    data >b400          ; mask for 16 bit
    .endif    

; Set SN Frequency: R0=freqency code, R2=channel command (MSB)
; Original: A=least significant byte  X=channel command  Y=most significant byte
sn76489_freq
    mov r0,r3
    andi r3,>000f
    swpb r3
    socb r3,r2
    movb r2,@SOUND  ; cmd and least significant nibble
    srl r0,4
    andi r0,>003f
    swpb r0
    movb r0,@SOUND  ; most significant byte
    b *r11

; Set SN volume: R0=volume (MSB, inverse of attenuation), R2=channel command (MSB)
; Original: A=volume (inverse of attenuation), X=channel command
sn76489_vol
    inv r0
    andi r0,>0f00
    socb r2,r0
    movb r0,@SOUND
    b *r11

; Set noise type: R0=Noise type (MSB)
; original: A=noise command
sn76489_control
    andi r0,>0f00
    ori r0,>e000
    movb r0,@SOUND
    b *r11

; Set up vdp generic settings - R0 should be preloaded with a register in MSB, data in LSB
; R2 should contain the color table entry (in MSB), R3 the bitmap table (in MSB). Rest is
; hard coded. WARNING: Disables interrupts but does not re-enable them.
vdp_generic_mode
    mov r11,r4      ; save return
    limi 0          ; ints off

    bl @WRTVDP      ; caller must set up this one
    li r0,>01a2     ; VDP mode, screen off
    bl @WRTVDP
    li r0,>0206     ; >1800 pattern table
    bl @WRTVDP
    li r0,>0003     ; for color table
    socb r2,r0
    swpb r0
    bl @WRTVDP
    li r0,>0004     ; for pattern table
    socb r3,r0
    swpb r0
    bl @WRTVDP
    li r0,>0536     ; >1b00 for sprite attribute table
    bl @WRTVDP
    li r0,>0607     ; >3800 for sprite pattern table
    bl @WRTVDP
    li r0,>0701     ; default screen color
    bl @WRTVDP
    b *r4

; set up VDP mode 0
mode_0
    mov r11,r8      ; careful - we call vdp_generic_mode and LDIRVM3
    li r0,>0800     ; bit we want to clear
    szcb r0,@mode

    li r2,>ff00	    ; $2000 for color table.
    li r3,>0300	    ; $0000 for bitmaps
    li r0,>0002     ; r0 setting
    bl @vdp_generic_mode    ; interrupts are now off

    limi 2
    limi 0

    li r0,>2000
    li r2,>f000
    li r3,>1800     ; fill color table with white on transparent
    bl @FILVRM

    limi 2
    bl @cls
    mov r8,r11      ; restore return address, and fall through to vdp_generic_sprites

; Initialize sprite table
vdp_generic_sprites
    mov r11,r8      ; save return address
    li r0,>1b00     ; sprite attribute table in VDP
    li r2,>d100     ; off screen, and otherwise unimportant
    li r3,128       ; number of bytes

    limi 0
    bl @FILVRM

    li r0,sprites
    li r2,>d1d1     ; write 2 bytes at a time
    li r3,128
!1
    mov r2,*r0+     ; initialize CPU mirror
    dect r3
    jne -!1

    li r0,>01e2     ; screen on
    bl @WRTVDP

    limi 2
    b *r8

; set up VDP mode 1
mode_1
    mov r11,r8      ; careful - we call vdp_generic_mode and LDIRVM3
    li r0,>0800     ; bit we want to clear
    szcb r0,@mode

    li r2,>ff00	    ; $2000 for color table.
    li r3,>0300	    ; $0000 for bitmaps
    li r0,>0002     ; r0 setting
    bl @vdp_generic_mode    ; interrupts are now off

    li r0,>0000
    li r2,>0000
    li r3,>1800
    bl @FILVRM      ; clear pattern table

    limi 2

    li r0,>2000
    li r2,>f000
    li r3,>1800

    limi 0
    bl @FILVRM      ; init color table
    limi 2

    li r0,>5800     ; >1800 with the write bit set
    clr r3          ; value to write

!1
    limi 0          ; write the screen image table, but pause every 32 bytes for interrupts

    swpb r0
    movb r0,@VDPWADR
    swpb r0
    movb r0,@VDPWADR

    li r2,32

!2
    movb r3,@VDPWDATA
    ai r3,>0100
    dec r2
    jne -!2

    limi 2
    ai r0,32
    ci r0,>5b00
    jl -!1

    mov r8,r11      ; restore return address
    jmp vdp_generic_sprites     ; using tail recursion

; Set up VDP mode 2
mode_2
    mov r11,r8      ; careful - we call vdp_generic_mode and LDIRVM3
    li r0,>0800     ; bit we want to set
    socb r0,@mode

    li r2,>8000	    ; $2000 for color table.
    li r3,>0000	    ; $0000 for bitmaps
    li r0,>0000     ; r0 setting
    bl @vdp_generic_mode    ; interrupts are now off

    limi 2
    limi 0

    li r0,>2000
    li r2,>f000
    li r3,>0020
    bl @FILVRM      ; init color table

    limi 2
    bl @cls         ; clear screen
    mov r8,r11      ; restore return
    b @vdp_generic_sprites

; this is where interrupts happen every frame
; Unlike a normal TI application, this one runs with interrupts ON,
; so all operations need to be sure to protect VDP address with LIMI 0,
; as well as any operations that might need to manipulate data managed
; by this interrupt. We enter via the normal user hook, so WP is on
; GPLWS, interrupts are off, and the VDP is already reset and status 
; stashed on vdp_status (>837b). Our return address to the ROM is in
; r11, but we are NOT going to use it so that we don't need to reserve
; r8 for whatever nonsense it does. That means we need to load intws
; and RTWP ourselves at the end. We are on our own workspace so we
; don't have to worry about the main app's workspace.
int_handler
; first copy the sprite table
    lwpi myintwp        ; separate safe workspace
    
    .ifne CVBASIC_BANK_SWITCHING
    mov @>7ffe,@saved_bank  ; save bank switch page
    .endif
    
    li r11,>005b        ; >1b00 with the write bit added, and byte flipped
    movb r11,@VDPWADR   ; SAL address
    swpb r11
    movb r11,@VDPWADR   ; going to copy the sprite table to VDP

    movb @mode,r11
    andi r11,>0400      ; if bit >04 (inhibit flicker) is cleared, jump ahead to rotate
    jeq !4

    clr r11             ; else we're going to just write it straight across
    li r12,128
    li r13,sprites
!7
    movb *r13+,@VDPWDATA
    dec r12
    jne -!7
    jmp !5

!4
    movb @flicker,r11   ; here we write it rotated every frame
    ai r11,>0400
    andi r11,>7f00
    movb r11,@flicker
    swpb r11            ; make count
    li r12,32           ; count

!6
    ai r11,sprites        ; this is still faster than separate incs
    movb *r11+,@VDPWDATA  ; copy one sprite
    movb *r11+,@VDPWDATA  ; no delay needed    
    movb *r11+,@VDPWDATA    
    movb *r11,@VDPWDATA   ; small optimization, since we have an add coming anyway
    ai r11,-(sprites-1)   ; remove address and add the rest of the increment
    andi r11,>007F        ; clamp it in range
    dec r12
    jne -!6
!5

; next read the joysticks - output needs to be 21xxLDRU - 1 and 2 are button and button2 respectively
; We don't have a button 2. We also need to read the keyboard and fill in key1_data. key2_data we
; will leave unused. Note key1_data expects Coleco-style 0-9,10-*,11-#,15=not pressed, but we can throw
; everything else as ASCII. We could do a split keyboard for 2 players, but I guess we'll leave it for now.
; joy1
    li r12,>0024    ; CRU base of select output
    li r13,>0600    ; joystick 1 column
    ldcr r13,3      ; select it
    src r12,7       ; delay
    li r12,>0006    ; CRU base of return read
    stcr r13,8      ; read 8 bits (we could get away with fewer, but be consistent)
    bl @convert_joystick
    movb r12,@joy1_data

; joy2
    li r12,>0024    ; CRU base of select output
    li r13,>0700    ; joystick 2 column
    ldcr r13,3      ; select it
    src r12,7       ; delay
    li r12,>0006    ; CRU base of return read
    stcr r13,8      ; read 8 bits (we could get away with fewer, but be consistent)
    bl @convert_joystick
    movb r12,@joy2_data
    
; do a quick modifier read for button 2 (control and fctn for joy 1 and 2 respectively)
    li r12,>0024    ; CRU base of select output
    clr r13         ; modifiers
    ldcr r13,3      ; select it
    src r12,7       ; delay
    li r12,>0006    ; CRU base of return read
    stcr r13,8      ; read 8 bits (we could get away with fewer, but be consistent)
    li r12,>4000    ; control
    li r11,>8000    ; button 2 bit
    czc r12,r13
    jne .noc1b2
    socb r11,@joy1_data
.noc1b2
    li r12,>1000    ; fctn
    czc r12,r13
    jne .noc2b2
    socb r11,@joy2_data
.noc2b2

; key1 - this is a very simple read with no modifiers, it just gives access to the letters and numbers
    clr r11         ; column
!key1
    li r12,>0024    ; CRU base of select output
    ldcr r11,3      ; select column
    src r12,7       ; delay
    li r12,>0006    ; CRU base of return read
    stcr r13,8      ; get the bits
    li r12,7        ; bit search
!key2
    sla r12,1
    czc @masktable(r12),r13    ; bit set?
    jne !key3       ; continue
    srl r12,1
    srl r11,5
    a r12,r11       ; calculate table offset
    movb @keyboard_table(r11),@key1_data    ; might be a dead key, but that's okay
    jmp !key4

!key3
    srl r12,1
    dec r12
    jgt -!key2
    jeq -!key2      ; we don't have a jump if not negative
    
    ai r11,>0100
    ci r11,>0600
    jne -!key1
    
    li r11,>0f00
    movb r11,@key1_data     ; no key was pressed
!key4

; check for quit - attempts to work it into the above were not working
; borrowed from console ROM
    li r12,>0024
    ldcr r12,3
    src r12,7
    li r12,>0006
    stcr r11,8
    li r12,>1100
    czc r12,r11
    jne .noquit
    clr @intwsr2
    blwp @>0000
.noquit    

    .ifne CVBASIC_MUSIC_PLAYER
    movb @music_mode,r0
    jeq !10
    bl @music_hardware
!10
    .endif

    inc @frame
    li r0,3
    a r0,@lfsr  ; Make LFSR more random

    .ifne CVBASIC_MUSIC_PLAYER
    movb @music_mode,r0
    jeq !9
    movb @music_frame,r0
    ai r0,>0100
    ci r0,>0500
    jl !11
    clr r0
    movb r0,@music_frame
    jhe !9

!11 movb r0,@music_frame
    bl @music_generate
!9
    .endif

    ;CVBASIC MARK DON'T CHANGE

; restore the saved bank
    .ifne CVBASIC_BANK_SWITCHING
    mov @saved_bank,r0  ; recover page switch
    clr *r0             ; switch it
    .endif

; get back the interrupt workspace and return
    lwpi INTWS
    RTWP

; given a joystick read in r13, return bits in r12
; The final output is 8 bits:
; 21xxLDRU - 1 and 2 are button and button2 respectively
; NOTE: if called by the compiler, this won't act as expected
convert_joystick
    clr r12
    czc @joystick_table,r13
    jne !j1
    ori r12,>0800
!j1
    czc @joystick_table+2,r13
    jne !j2
    ori r12,>0400
!j2
    czc @joystick_table+4,r13
    jne !j3
    ori r12,>0200
!j3
    czc @joystick_table+6,r13
    jne !j4
    ori r12,>0100
!j4
    czc @joystick_table+8,r13
    jne !j5
    ori r12,>4000
!j5
    b *r11

joystick_table
    data >0200,>0800,>0400,>1000,>0100    ; LDRU1

; By columns, then rows. 8 Rows per column. No shift states - converted to the Coleco returns
; for numbers, , and . become ; and #. Control is control1 button2, and Fctn is control2 button2
keyboard_table
    byte 61,32,11,15,254,15,255,15  ; '=',' ',enter,n/a,fctn,shift,ctrl,n/a
    byte 46,76,79,9,2,83,87,88      ; '.','L','O','9','2','S','W','X'
    byte 44,75,73,8,3,68,69,67      ; ',','K','I','8','3','D','E','C'
    byte 77,74,85,7,4,70,82,86      ; 'M','J','U','7','4','F','R','V'
    byte 78,72,89,6,5,71,84,66      ; 'N','H','Y','6','5','G','T','B'
    byte 10,59,80,0,1,65,81,90      ; '/',';','P','0','1','A','Q','Z'

masktable
    data >0100,>0200,>0400,>0800,>1000,>2000,>4000,>8000
    data >0f00
    
; wait for frame to increment
wait
    mov @frame,r0
!1
    c r0,@frame
    jeq -!1
    b *r11

; initialize music system
music_init
; mute sound chip
    li r0,>9fbf
    movb r0,@SOUND
    swpb r0
    movb r0,@SOUND
    li r0,>dfff
    movb r0,@SOUND
    swpb r0
    movb r0,@SOUND

    .ifeq CVBASIC_MUSIC_PLAYER
; return if we don't have the player compiled in    
    b *r11
    .endif

; all the rest of the player is under this if    
    .ifne CVBASIC_MUSIC_PLAYER

; set some... things?    
    li r0,>ff00
    movb r0,@audio_vol4hw
    swpb r0
    movb r0,@audio_control

; set up silence, and fall through into music_play
    li r0,music_silence

;
; Play music.
; R0 = Pointer to music (original YYAA)
;
music_play
    limi 0                          ; ints off
    clr r2                          ; get a zero
    movb r2,@music_note_counter     ; store in the counter
    movb *r0+,r2                    ; fetch the first byte of the music and increment
    mov r0,@music_pointer           ; store the updated address
    movb r2,@music_timing           ; store fetched byte in timing
    li r2,>0100
    movb r2,@music_playing          ; needs to be a 1 for BASIC
    mov @music_pointer,@music_start  ; remember this point

    .ifne CVBASIC_BANK_SWITCHING
    mov @>7ffe,@music_bank          ; save bank switch page for music
    .endif
    
    limi 2                          ; ints back on
    b *r11                          ; back to caller

;
; Generates music - called from interrupt (regs are saved)
;
music_generate
    mov r11,r6
    clr r4                          ; use r4 as a zero
    movb r4,@audio_vol1
    movb r4,@audio_vol2
    movb r4,@audio_vol3
    li r0,>ff00
    movb r0,@audio_vol4hw
    movb @music_note_counter,r0     ; check countdown
    jne !2                          ; if not zero, skip ahead
!1
    .ifne CVBASIC_BANK_SWITCHING
    mov @music_bank,r0              ; get music bank switch page
    clr *r0                         ; set it
    .endif

    mov @music_pointer,r1           ; keep music pointer in r1 - update it if needed
    clr r0
    movb *r1,r0                     ; checking if first byte of pack is loop or end
    ci r0,>fe00                     ; end of music?
    jne !3                          ; nope, jump ahead
    movb r4,@music_playing          ; keep at same place
    b *r6

!3	
    ci r0,>fd00                     ; repeat?
    jne !4                          ; nope, skip
    mov @music_start,@music_pointer  ; yep, copy back the loop point
    jmp -!1                          ; and start again (So music that STARTS with FD will spin forever - bug)

!4
    movb @music_timing,r0
    andi r0,>3f00                   ; restart note time
    movb r0,@music_note_counter
    movb *r1+,r0                    ; fetch byte and increment
    ci r0,>3f00                     ; sustain?
    jeq !5
    mov r0,r2
    andi r2,>c000
    movb r2,@music_instrument_1     ; save instrument type
    andi r0,>3f00
    movb r0,@music_note_1           ; save note
    movb r4,@music_counter_1        ; and reset count

!5
    movb *r1+,r0                    ; fetch next byte and increment
    ci r0,>3f00                     ; sustain
    jeq !6
    mov r0,r2
    andi r2,>c000
    movb r2,@music_instrument_2
    andi r0,>3f00
    movb r0,@music_note_2
    movb r4,@music_counter_2
    
!6
    movb *r1+,r0                    ; fetch next byte and increment
    ci r0,>3f00                     ; sustain
    jeq !7
    mov r0,r2
    andi r2,>c000
    movb r2,@music_instrument_3
    andi r0,>3f00
    movb r0,@music_note_3
    movb r4,@music_counter_3
    
!7
    movb *r1+,r0                    ; fetch drum byte and increment
    movb r0,@music_drum
    movb r4,@music_counter_4
    mov r1,@music_pointer           ; this brings music_pointer up to date - done with r1
    
!2
    clr r2
    movb @music_note_1,r2
    jeq !8
    movb @music_instrument_1,r0
    movb @music_counter_1,r1
    bl @music_note2freq
    mov r0,@audio_freq1
    movb r1,@audio_vol1

!8
    movb @music_note_2,r2
    jeq !9
    movb @music_instrument_2,r0
    movb @music_counter_2,r1
    bl @music_note2freq
    mov r0,@audio_freq2
    movb r1,@audio_vol2
    
!9
    movb @music_note_3,r2
    jeq !10
    movb @music_instrument_3,r0
    movb @music_counter_3,r1
    bl @music_note2freq
    mov r0,@audio_freq3
    movb r1,@audio_vol3
    
!10
    clr r2
    movb @music_drum,r2
    jeq !11
    ci r2,>0100                     ; 1 - long drum
    jne !12
    movb @music_counter_4,r2
    ci r2,>0300
    jhe !11
    
!15
    li r0,>ecf5
    movb r0,@audio_noise
    swpb r0
    movb r0,@audio_vol4hw
    jmp !11

!12	
    ci r2,>0200                     ; 2 - short drum
    jne !14
    movb @music_counter_4,r2
    jne !11                         ; was an explicit cmp #0 in original code... needed?
    li r0,>edf5
    movb r0,@audio_noise
    swpb r0
    movb r0,@audio_vol4hw
    jmp !11

!14
; 3 - Roll was commented out...
    movb @music_counter_4,r2
    ci r2,>0200
    jl -!15
    sla r2,1
    sb @music_timing,r2
    jnc !11
    ci r2,>0400
    jl -!15

!11
    clr r1
    li r2,>1000

    movb @music_counter_1,r1
    ai r1,>0100
    ci r1,>1800
    jne $+6
    li r1,>1000
    movb r1,@music_counter_1
   
    movb @music_counter_2,r1
    ai r1,>0100
    ci r1,>1800
    jne $+6
    li r1,>1000
    movb r1,@music_counter_2

    movb @music_counter_3,r1
    ai r1,>0100
    ci r1,>1800
    jne $+6
    li r1,>1000
    movb r1,@music_counter_3

    li r1,>0100
    ab r1,@music_counter_4
    sb r1,@music_note_counter
    b *r6

;
; flute instrument
;
music_flute
    mov @music_notes_table(r2),r0
    movb @flutenote2(r1),r2
    sra r2,8
    a r2,r0
    movb @flutevol1(r1),r1
    b *r11
    
flutevol1
    byte 10,12,13,13,12,12,12,12
    byte 11,11,11,11,10,10,10,10
    byte 11,11,11,11,10,10,10,10

flutenote2
    byte 0,0,0,0,0,1,1,1
    byte 0,1,1,1,0,1,1,1
    byte 0,1,1,1,0,1,1,1

    ;
    ; Converts note to frequency.
    ; Input
    ;   A = Instrument         (r0 - msb)
    ;   X = Instrument counter (r1 - msb)
    ;   Y = Note (1-62)        (r2 - msb)
    ; Output
    ;   YA = Frequency. (r0 - word)
    ;   X = Volume.     (r1 - msb)
    ;
music_note2freq
    srl r2,8                ; make int so they can be indexes
    sla r2,1                ; make word index
    srl r1,8                ; just byte index here
    swpb r0
    movb r1,r0              ; conveniently we now know it's >00 so we can clear out r0's LSB for easier tests
    swpb r0
    
    ci r0,>4000
    jl music_piano
    jeq music_clarinet
    ci r0,>8000
    jeq music_flute
    
;
; Bass instrument
; 
music_bass
    mov @music_notes_table(r2),r0
    sla r0,1
    movb @bassvol1(r1),r1
    b *r11

bassvol1
    byte 13,13,12,12,11,11,10,10
    byte 9,9,8,8,7,7,6,6
    byte 5,5,4,4,3,3,2,2

;
; Piano instrument
; 
music_piano
    mov @music_notes_table(r2),r0
    movb @pianovol1(r1),r1
    b *r11

pianovol1	
    byte 12,11,11,10,10,9,9,8
    byte 8,7,7,6,6,5,5,4
    byte 4,4,5,5,4,4,3,3

;
; Clarinet instrument
;
music_clarinet
    mov @music_notes_table(r2),r0
    srl r0,1
    jnc !1
    inc r0
!1
    movb @clarinetnote2(r1),r2
    sra r2,8
    a r2,r0
    movb @clarinetvol1(r1),r1   ; msb only?
    b *r11

clarinetvol1
    byte 13,14,14,13,13,12,12,12
    byte 11,11,11,11,12,12,12,12
    byte 11,11,11,11,12,12,12,12

clarinetnote2
    byte 0,0,0,0,-1,-2,-1,0
    byte 1,2,1,0,-1,-2,-1,0
    byte 1,2,1,0,-1,-2,-1,0

    ;
    ; Musical notes table.
    ;
music_notes_table
    ; Silence - 1 - Note: the TI sound chip is not mute at 0, it's actually 0x400. 1 is beyond hearing range.
    data 1
	; Values for 3.58 mhz.
	; 2nd octave - Index 1
	data 1710,1614,1524,1438,1357,1281,1209,1141,1077,1017,960,906
	; 3rd octave - Index 13
	data 855,807,762,719,679,641,605,571,539,508,480,453
	; 4th octave - Index 25
	data 428,404,381,360,339,320,302,285,269,254,240,226
	; 5th octave - Index 37
	data 214,202,190,180,170,160,151,143,135,127,120,113
	; 6th octave - Index 49
	data 107,101,95,90,85,80,76,71,67,64,60,57
	; 7th octave - Index 61
	data 53,50,48

; handle the hardware side of the music player
music_hardware
    clr r0
    movb @music_mode,r0
    ci r0,>0400         ; play simple?
    jl !7               ; yes, jump

    movb @audio_vol2,r0  ; what is this block's intent?
    jne !7
    movb @audio_vol3,r0
    jeq !7
    movb r0,@audio_vol2
    clr r0
    movb r0,@audio_vol3
    mov @audio_freq3,@audio_freq2
    
!7
    li r1,>9f00         ; mute default
    mov @audio_freq1,r0
    ci r0,>0400         ; filter out of range
    jhe !1
    mov r0,r1           ; write least significant plus command nibble
    swpb r1
    andi r1,>0fff
    ori r1,>8000
    movb r1,@SOUND      ; command + least significant nibble
    sla r0,4
    movb r0,@SOUND      ; most significant byte
    movb @audio_vol1,r2
    srl r2,8
    movb @ay2sn(r2),r1  ; translate from AY to SN
    ori r1,>9000
    
!1    
    movb r1,@SOUND      ; volume

    li r1,>bf00         ; mute default
    mov @audio_freq2,r0
    ci r0,>0400
    jhe !2
    mov r0,r1           ; write least significant plus command nibble
    swpb r1
    andi r1,>0fff
    ori r1,>a000
    movb r1,@SOUND      ; command + least significant nibble
    sla r0,4
    movb r0,@SOUND      ; most significant byte
    movb @audio_vol2,r2
    srl r2,8
    movb @ay2sn(r2),r1  ; translate from AY to SN
    ori r1,>b000
    
!2    
    movb r1,@SOUND      ; volume

    clr r0
    movb @music_mode,r0
    ci r0,>0400         ; play simple?
    jl !6               ; yes jump
    
    li r1,>df00         ; mute default
    mov @audio_freq3,r0
    ci r0,>0400
    jhe !3
    mov r0,r1           ; write least significant plus command nibble
    swpb r1
    andi r1,>0fff
    ori r1,>c000
    movb r1,@SOUND      ; command + least significant nibble
    sla r0,4
    movb r0,@SOUND      ; most significant byte
    movb @audio_vol3,r2
    srl r2,8
    movb @ay2sn(r2),r1  ; translate from AY to SN
    ori r1,>d000
    
!3
    movb r1,@SOUND      ; volume

!6
    movb @music_mode,r0
    andi r0,>0100       ; check for drums
    jeq !8
    
    movb @audio_vol4hw,r0
    ci r0,>ff00
    jeq !4
    
    movb @audio_noise,r1
    cb r1,@audio_control    ; don't retrigger noise if same
    jeq !4
    movb r1,@audio_control
    movb r1,@SOUND      ; noise type
    
!4
    movb r0,@SOUND      ; noise volume

!8
    b *r11

;
; Converts AY-3-8910 volume to SN76489
;
ay2sn
    byte >0f,>0f,>0f,>0e,>0e,>0e,>0d,>0b,>0a,>08,>07,>05,>04,>03,>01,>00

; default silent tune to play when idle
music_silence
    byte 8
    byte 0,0,0,0
    byte -2

; endif for CVBASIC_MUSIC_PLAYER
    .endif       


    .ifne CVBASIC_COMPRESSION

; Load compressed character definitions: Char number in R4, CPU data in R0, count in R5 (MSB)
; Original: pointer = char number, temp = CPU address, a = number chars
define_char_unpack
    mov r0,r2
    andi r4,>00ff   ; mask off to 0-255
    sla r4,3        ; times 8
    movb @mode,r0   ; get mode
    andi r0,>0800   ; check bitmap bit
    jeq unpack3     ; 3 times if yes
    jmp unpack      ; once if no

; Load bitmap color definitions: Char number in R4, CPU data in R0, count in R5 (MSB)
; Original: pointer = char number, temp = CPU address, a = number chars
define_color_unpack
    mov r0,r2
    andi r4,>00ff   ; mask off to 0-255
    sla r4,3        ; char times 8
    li r0,>4000     ; base of color table
    a r0,r4         ; set base for color then fall through

; entered from one of the above two functions    
unpack3
    mov r11,r9      ; save return address
    mov r4,r10      ; save VDP address
    mov r2,r4
    bl @unpack
    ai r10,>800
    mov r10,r1
    mov r4,r2
    bl @unpack
    ai r10,>800
    mov r10,r1
    mov r4,r2
    bl @unpack
    b *r9

;
; Pletter-0.5c decompressor (XL2S Entertainment & Team Bomba)
; Ported by hand from https://gitea.zaclys.com/Mokona/Unpletter/src/branch/main/pletter.cpp
; Unpack data to VDP: VDP address in R1, CPU data in R2
; Original: pointer = VDP, temp = CPU address, a = number chars

; The challenge with porting is the 8 bit vs 16-bit registers, and the behaviour of rotate
; 9900 shifts and rotates are always 16 bits wide, and do not include the carry bit
; rather, shifted out bits are /copied/ to carry, but carry is never copied in. This makes
; most of the optimized code difficult to port - but if we knew the original intent, it
; wouldn't be so bad. In addition, the 9900 has very few instructions that preserve
; status flags, meaning we can't execute an instruction and then check status for the
; instruction before it in nearly any case, the code uses this too. 
; I've not been able to locate any C code for the unpacker which would probably be easier to port.

r0lsb   equ mywp+1
r1lsb   equ mywp+3
r2lsb   equ mywp+5
r3lsb   equ mywp+7
r4lsb   equ mywp+9
r5lsb   equ mywp+11
r6lsb   equ mywp+13
r7lsb   equ mywp+15
r8lsb   equ mywp+17
r13lsb  equ mywp+27

unpack
; Initialization
    mov r11,r12         ; save return
    
    clr r3
    movb *r2+,r3       ; lda (temp),y

    clr r5              ; ldy #0
    sla r3,1
    jnc !up14
    inc r5
!up14
    ai r3,>0100
    sla r5,1
    sla r3,1
    jnc !up15
    inc r5
!up15
    sla r5,1
    sla r3,1
    jnc !up16
    inc r5
!up16
    sla r5,1

    ai r5,!modes          
    mov *r5,r6          

!literal
    mov r3,r7      
    movb *r2+,r3
    
    mov r1,r0
    ori r0,>4000
    swpb r0
    limi 0
    movb r0,@VDPWADR
    swpb r0
    movb r0,@VDPWADR
    movb r3,@VDPWDATA
    limi 2              ; inline setup/write WRTVRM

    inc r1              ; inc pointer / bne $+4 / inc pointer+1

    mov r7,r3           ; lda pletter_bit
!loop
    sla r3,1            ; asl a
    jne !up2            ; bne $+5
    bl @!getbit         ; jsr .getbit
!up2
    jnc -!literal       ; bcc .literal

    ; Compressed data
    li r13,>0001        ; ldx #1 / stx result / dex / stx result+1
    
!getlen
    sla r3,1            ; asl a
    jne !up3            ; bne $+5
    bl @!getbit         ; jsr .getbit
!up3
    jnc !lenok          ; bcc .lenok
    
!lus
    sla r13,1
    jnc !up5
    b *r12
!up5
    sla r3,1            ; asl a
    jne !up4            ; bne $+5
    bl @!getbit         ; jsr .getbit
!up4
    jnc !up6            ; need to check carry now...
    inc r13    
!up6
    sla r3,1            ; asl a
    jne !up7            ; bne $+5
    bl @!getbit         ; jsr .getbit
!up7
    jnc !lenok          ; bcc .lenok

    sla r13,1
    jnc !up9
    b *r12
!up9
    sla r3,1            ; asl a
    jne !up8            ; bne $+5
    bl @!getbit         ; jsr .getbit
!up8
    jnc !up10           ; bcc $+3
    inc r13
!up10
    sla r3,1            ; asl a
    jne !up11           ; bne $+5
    bl @!getbit         ; jsr .getbit
!up11
    joc -!lus           ; bcs .lus
    
!lenok
    inc r13             ; inc result / bne $+4 / inc result+1
    mov r3,r7           ; sta pletter_bit

    clr r8
    movb *r2+,r8        ; lda (temp),y
    swpb r8             ; sta pletter_off / lda pletter_off
    
    ci r8,>0080
    jl !offsok

    mov r7,r3           ; lda pletter_bit
    b *r6               ; jmp (temp2)
    
!mode6
    sla r3,1            ; asl a
    jne !m6p            ; bne $+5
    bl @!getbit         ; jsr .getbit
!m6p
    clr r0
    jnc !m6p2
    li r0,>8000
!m6p2
    movb r8,@r0lsb
    src r0,15
    movb @r0lsb,r8      ; rol pletter_off+1
!mode5
    sla r3,1            ; asl a
    jne !m5p            ; bne $+5
    bl @!getbit         ; jsr .getbit
!m5p
    clr r0
    jnc !m5p2
    li r0,>8000
!m5p2
    movb r8,@r0lsb
    src r0,15
    movb @r0lsb,r8      ; rol pletter_off+1
!mode4
    sla r3,1            ; asl a
    jne !m4p            ; bne $+5
    bl @!getbit         ; jsr .getbit
!m4p
    clr r0
    jnc !m4p2
    li r0,>8000
!m4p2
    movb r8,@r0lsb
    src r0,15
    movb @r0lsb,r8      ; rol pletter_off+1
!mode3
    sla r3,1            ; asl a
    jne !m3p            ; bne $+5
    bl @!getbit         ; jsr .getbit
!m3p
    clr r0
    jnc !m3p2
    li r0,>8000
!m3p2
    movb r8,@r0lsb
    src r0,15
    movb @r0lsb,r8      ; rol pletter_off+1
!mode2
    sla r3,1            ; asl a
    jne !m2p            ; bne $+5
    bl @!getbit         ; jsr .getbit
!m2p
    clr r0
    jnc !m2p2
    li r0,>8000
!m2p2
    movb r8,@r0lsb
    src r0,15
    movb @r0lsb,r8      ; rol pletter_off+1

    sla r3,1            ; asl a
    jne !m2p3           ; bne $+5
    bl @!getbit         ; jsr .getbit
!m2p3

    mov r3,r7           ; sta pletter_bit (no touch carry)
    jnc !offsok         ; bcc .offsok

    ai r8,>0100         ; inc pletter_off+1
    andi r8,>ff7f       ; lda pletter_off / and #$7f / sta pletter_off

!offsok
    inc r8              ; inc pletter_off / bne $+4 / inc pletter_off+1
    
    mov r1,r0
    s r8,r0
    mov r0,r8           ; lda pointer / sec / sbc pletter_off / sta pletter_off / lda pointer+1 / sbc pletter_off+1 / sta pletter_off+1
    ori r1,>4000        ; do this outside the loop
    
!loop2
    limi 0              ; sei
    
    swpb r8
    movb r8,@VDPWADR
    swpb r8
    movb r8,@VDPWADR    ; RDVRM from pletter_off
    nop
    movb @VDPDATA,r0
    
    swpb r1
    movb r1,@VDPWADR
    swpb r1
    movb r1,@VDPWADR
    movb r0,@VDPWDATA   ; WRTVRM to pointer

    limi 2              ; cli
    inc r8              ; inc pletter_off / bne $+4 / inc pletter_off+1
    inc r1              ; inc pointer / bne $+4 / inc pointer+1
    dec r13
    jne -!loop2         ; dec result / bne .loop2 / dec result+1 / bne .loop2

    andi r1,>3fff       ; restore address
    mov r7,r3           ; lda pletter_bit
    b @-!loop           ; jmp .loop

!getbit
    clr r3              ; ldy #0
    movb *r2+,r3        ; lda (temp),y / inc temp / bne $+4 / inc temp+1
    joc !gb1
    sla r3,1            ; rol a with no carry
    b *r11
!gb1
    sla r3,1
    ori r3,>0100        ; rol a with carry
    b *r11

!modes
    data -!offsok
    data -!mode2
    data -!mode3
    data -!mode4
    data -!mode5
    data -!mode6
    .endif

; The stack is simulated using R10. It's not used in
; these functions but the compiled code will need it.
; To push: dect r10, mov r0,*r10
; To pop:  mov *r10+,r0
; However, for JSR a function is helpful - call this like:
; bl @jsr
; data <function_address>
jsr
    mov *r11+,r14       ; get the jump address
    dect r10            ; make room on stack
    mov r11,*r10        ; save return address
    bl *r14             ; new subroutine call, we can come back here
    mov *r10+,r11       ; get real return off stack - warning, all basic functions do this inline rather than return
    b *r11              ; back to caller

; entry code - we should enter with ints off anyway
START
    limi 0
    lwpi mywp           ; get our private workspace
    li R10,>4000        ; pseudo stack pointer
    movb @>8802,@vdp_status  ; clear any pending VDP interrupt and initialize vdp_status if needed
    
    li r0,>0182         ; select 16k, magnified, blank, no ints
    bl @wrtvdp          ; other ports write this twice... maybe to be 100% sure no NMI happens? We don't have that problem.    

    li r0,firstsp       ; clear variables in scratchpad
stlp1
    clr *r0+
    ci r0,lastsp
    jl stlp1
    
    li r0,>2000         ; clear lower 8k RAM
stlp2
    clr *r0+
    ci r0,>4000
    jne stlp2

    li r0,>0f0f
    mov r0,@key1_data   ; gets both - no key is >0f return
    
    bl @music_init
    bl @mode_0

    li r0,int_handler
    mov r0,@intwsr2     ; set interrupt function
    li r0,>8000
    mov r0,@intwsr1     ; disable most console ROM interrupt handling
    li r1,1
    mov r1,@intwsr11    ; make sure screen timeout is odd so it never triggers
    li r1,>0108
    mov r1,@gplwsr14    ; GPL status flags, should already be this, but be sure
    movb r1,@ntsc       ; init ntsc flag to true - we actually do not have a good way to detect
    li r1,>8c02
    mov r1,@gplwsr15    ; Address of VDP for GPL - should already be this, but be sure
    
;;; CVBasic code starts here

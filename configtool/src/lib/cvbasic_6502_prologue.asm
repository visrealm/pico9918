	;
	; CVBasic prologue (BASIC compiler, 6502 target)
	;
	; by Oscar Toledo G.
	; https://nanochess.org/
	;
	; Creation date: Aug/05/2024.
	; Revision date: Aug/06/2024. Ported music player from Z80 CVBasic.
	; Revision date: Aug/07/2024. Ported Pletter decompressor from Z80 CVBasic.
	;                             Added VDP delays.
	; Revision date: Aug/16/2024. Corrected bug in define_char_unpack.
	; Revision date: Aug/21/2024. Added support for keypad.
	; Revision date: Aug/30/2024. Changed mode bit to bit 3 (avoids collision
	;                             with flicker flag).
	; Revision date: Oct/15/2024. Added LDIRMV.
	; Revision date: Nov/12/2024. Saves the VDP status.
	;

	CPU 6502

BIOS_NMI_RESET_ADDR:	EQU $F808
BIOS_READ_CONTROLLERS:	EQU $FA00
BIOS_WRITE_PSG:		EQU $FE77


VDP_WRITE_DATA:   EQU $3000
VDP_WRITE_REG:    EQU $3001
VDP_READ_DATA:    EQU $2000
VDP_READ_STATUS:  EQU $2001

INCLUDE_FONT_DATA: EQU 0

	;
	; Platforms supported:
	; o Vtech Creativision.
	; o Dick Smith's Wizzard.
	;

	;
	; CVBasic variables in zero page.
	;

	; This is a block of 8 bytes that should stay together.
temp:		equ $02
temp2:		equ $04
result:		equ $06
pointer:	equ $08

read_pointer:	equ $0a
cursor:		equ $0c
pletter_off:	equ $0e	; Used by Pletter

	; Zero page $00-$01 and $10-$1f are used by
	; the Creativision BIOS to read the controllers.
joy1_dir:	equ $11
joy2_dir:	equ $13
joy1_buttons:	equ $16
joy2_buttons:	equ $17

joy1_data:	equ $20
joy2_data:	equ $21
key1_data:	equ $22
key2_data:	equ $23
frame:		equ $24
lfsr:		equ $26
mode:           equ $28
flicker:	equ $29
sprite_data:	equ $2a
ntsc:		equ $2e
pletter_bit:	equ $2f
vdp_status:	equ $30

	IF CVBASIC_MUSIC_PLAYER
music_playing:		EQU $4f
music_timing:		EQU $31
music_start:		EQU $32
music_pointer:		EQU $34
music_note_counter:	EQU $36
music_instrument_1:	EQU $37
music_note_1:		EQU $38
music_counter_1:	EQU $39
music_instrument_2:	EQU $3a
music_note_2:		EQU $3b
music_counter_2:	EQU $3c
music_instrument_3:	EQU $3d
music_note_3:		EQU $3e
music_counter_3:	EQU $3f
music_drum:		EQU $40
music_counter_4:	EQU $41
audio_freq1:		EQU $42
audio_freq2:		EQU $44
audio_freq3:		EQU $46
audio_vol1:		EQU $48
audio_vol2:		EQU $49
audio_vol3:		EQU $4a
audio_vol4hw:		EQU $4b
audio_noise:		EQU $4c
audio_control:		EQU $4d
music_mode:		EQU $4e
	ENDIF

sprites:	equ $0180

	ORG $4000+$4000*SMALL_ROM
	
WRTVDP:
	STA VDP_WRITE_REG
	TXA
	ORA #$80
	STA VDP_WRITE_REG
	RTS

SETWRT:
	STA VDP_WRITE_REG	; 4
	TYA		; 2
	ORA #$40	; 2
	STA VDP_WRITE_REG	; 4
	RTS		; 6

SETRD:
	STA VDP_WRITE_REG	; 4
	TYA		; 2
	AND #$3F	; 2
	STA VDP_WRITE_REG	; 4
	RTS		; 6

	; VDP delays calculated for 6502 running at 2 mhz.
WRTVRM:
	JSR SETWRT	; 6
	TXA		; 2
	NOP		; 2
	NOP		; 2
	NOP		; 2 = RTS + 14 = Minimum cycles
	NOP		; 2
	STA VDP_WRITE_DATA	; 4
	RTS		; 6

RDVRM:
	JSR SETRD	; 6
	NOP		; 2
	NOP		; 2
	NOP		; 2
	NOP		; 2
	NOP		; 2
	NOP		; 2
	NOP		; 2
	NOP		; 2
	NOP		; 2
	NOP		; 2
	NOP		; 2
	LDA VDP_READ_DATA	; 4
	RTS		; 6

; Read the status register from VDP - data returned in A (visrealm)
RDVST:
    LDA VDP_READ_STATUS
    RTS

FILVRM:
	LDA pointer
	LDY pointer+1
	JSR SETWRT
	LDA temp2
	BEQ .1
	INC temp2+1
.1:
	LDA temp	; 3
	STA VDP_WRITE_DATA	; 4
	NOP		; 2
	NOP		; 2
	DEC temp2	; 5
	BNE .1		; 2/3/4
	DEC temp2+1	; 5
	BNE .1		; 2/3/4
	RTS	

LDIRMV:
	LDA temp
	LDY temp+1
	JSR SETRD
	LDA temp2
	BEQ .1
	INC temp2+1
.1:
	LDY #0
.2:
	LDA VDP_WRITE_DATA	; 4
	STA (pointer),Y	; 5/6
	INC pointer	; 5
	BNE .3		; 2/3/4
	INC pointer+1	; 5
.3:
	DEC temp2	; 5
	BNE .2		; 2/3/4
	DEC temp2+1	; 5
	BNE .2		; 2/3/4
	RTS

LDIRVM:
	LDA pointer
	LDY pointer+1
	JSR SETWRT
	LDA temp2
	BEQ .1
	INC temp2+1
.1:
	LDY #0
.2:
	LDA (temp),Y	; 5/6
	STA VDP_WRITE_DATA	; 4
	INC temp	; 5
	BNE .3		; 2/3/4
	INC temp+1	; 5
.3:
	DEC temp2	; 5
	BNE .2		; 2/3/4
	DEC temp2+1	; 5
	BNE .2		; 2/3/4
	RTS

LDIRVM3:
	JSR .1
	JSR .1
.1:	LDA temp
	PHA
	LDA temp+1
	PHA
	LDA temp2
	PHA
	LDA temp2+1
	PHA
	JSR LDIRVM
	LDA pointer+1
	CLC
	ADC #8
	STA pointer+1
	PLA
	STA temp2+1
	PLA
	STA temp2
	PLA
	STA temp+1
	PLA
	STA temp
	RTS

DISSCR:
	SEI
	LDA #$A2
	LDX #$01
	JSR WRTVDP
	CLI
	RTS

ENASCR:
	SEI
	LDA #$E2
	LDX #$01
	JSR WRTVDP
	CLI
	RTS

CPYBLK:
	SEI
.1:	
	LDA temp2
	PHA
	LDA temp2+1
	PHA
	TXA
	PHA
	TYA
	PHA
	LDA temp
	PHA
	LDA temp+1
	PHA
	LDA #0
	STA temp2+1
	JSR LDIRVM
	PLA
	STA temp+1
	PLA
	STA temp
	PLA
	STA temp2+1
	PLA
	STA temp2
	LDA temp
	CLC
	ADC temp2
	STA temp
	LDA temp+1
	ADC temp2+1
	STA temp+1
	LDX temp2
	LDY temp2+1
	PLA
	STA temp2+1
	PLA
	STA temp2
	LDA pointer
	CLC
	ADC #$20
	STA pointer
	LDA pointer+1
	ADC #$00
	STA pointer+1
	DEC temp2+1
	BNE .1
	CLI
	RTS

cls:
	lda #$00
	ldy #$18
	sta cursor
	sty cursor+1
	sta pointer
	sty pointer+1
	ldy #$03
	sta temp2
	sty temp2+1
	lda #$20
	sta temp
	sei
	jsr FILVRM
	cli
	rts

print_string_cursor_constant:
	PLA
	STA temp
	PLA
	STA temp+1
	LDY #1
	LDA (temp),Y
	STA cursor
	INY
	LDA (temp),Y
	STA cursor+1
	INY
	LDA (temp),Y
	STA temp2
	TYA
	CLC
	ADC temp
	STA temp
	BCC $+4
	INC temp+1
	LDA temp2
	BNE print_string.2

print_string_cursor:
	STA cursor
	STY cursor+1
print_string:
	PLA
	STA temp
	PLA
	STA temp+1
	LDY #1
	LDA (temp),Y
	STA temp2
	INC temp
	BNE $+4
	INC temp+1
.2:	CLC
	ADC temp
	TAY
	LDA #0
	ADC temp+1
	PHA
	TYA
	PHA
	INC temp
	BNE $+4
	INC temp+1
	LDA temp2
	PHA
	LDA #0
	STA temp2+1
	LDA cursor
	STA pointer
	LDA cursor+1
	AND #$07
	ORA #$18
	STA pointer+1
	SEI
	JSR LDIRVM
	CLI
	PLA
	CLC
	ADC cursor
	STA cursor
	BCC .1
	INC cursor+1
.1:	
	RTS

print_number:
	LDX #0
	STX temp
	SEI
print_number5:
	LDX #10000
	STX temp2
	LDX #10000/256
	STX temp2+1
	JSR print_digit
print_number4:
	LDX #1000
	STX temp2
	LDX #1000/256
	STX temp2+1
	JSR print_digit
print_number3:
	LDX #100
	STX temp2
	LDX #0
	STX temp2+1
	JSR print_digit
print_number2:
	LDX #10
	STX temp2
	LDX #0
	STX temp2+1
	JSR print_digit
print_number1:
	LDX #1
	STX temp2
	STX temp
	LDX #0
	STX temp2+1
	JSR print_digit
	CLI
	RTS

print_digit:
	LDX #$2F
.2:
	INX
	SEC
	SBC temp2
	PHA
	TYA
	SBC temp2+1
	TAY
	PLA
	BCS .2
	CLC
	ADC temp2
	PHA
	TYA
	ADC temp2+1
	TAY
	PLA
	CPX #$30
	BNE .3
	LDX temp
	BNE .4
	RTS

.4:	DEX
	BEQ .6
	LDX temp+1
	BNE print_char
.6:
	LDX #$30
.3:	PHA
	LDA #1
	STA temp
	PLA

print_char:
	PHA
	TYA
	PHA
	LDA cursor+1
	AND #$07
	ORA #$18
	TAY
	LDA cursor
	JSR WRTVRM
	INC cursor
	BNE .1
	INC cursor+1
.1:
	PLA
	TAY
	PLA
	RTS

define_sprite:
	sta temp2
	lda #0
	sta temp2+1
	lda #7
	sta pointer+1
	lda pointer
	asl a
	asl a
	asl a
	rol pointer+1
	asl a
	rol pointer+1
	asl a
	rol pointer+1
	sta pointer
	lda temp2
	asl a
	rol temp2+1
	asl a
	rol temp2+1
	asl a
	rol temp2+1
	asl a
	rol temp2+1
	asl a
	rol temp2+1
	sta temp2
	sei
	jsr LDIRVM
	cli
	rts

define_char:
	sta temp2
	lda #0
	sta pointer+1
	sta temp2+1
	lda pointer
	asl a
	rol pointer+1
	asl a
	rol pointer+1
	asl a
	rol pointer+1
	sta pointer
	lda temp2
	asl a
	rol temp2+1
	asl a
	rol temp2+1
	asl a
	rol temp2+1
	sta temp2
	sei
	lda mode
	and #$08
	bne .1
	jsr LDIRVM3
	cli
	rts

.1:	jsr LDIRVM
	cli
	rts

define_color:
	sta temp2
	lda #0
	sta temp2+1
	lda #$04
	sta pointer+1
	lda pointer
	asl a
	rol pointer+1
	asl a
	rol pointer+1
	asl a
	rol pointer+1
	sta pointer
	lda temp2
	asl a
	rol temp2+1
	asl a
	rol temp2+1
	asl a
	rol temp2+1
	sta temp2
	sei
	jsr LDIRVM3
	cli
	rts

update_sprite:
	ASL A
	ASL A
	ORA #$80
	STA pointer
	LDA #$01
	STA pointer+1
	LDY #0
	LDA sprite_data+0
	STA (pointer),Y
	INY
	LDA sprite_data+1
	STA (pointer),Y
	INY
	LDA sprite_data+2
	STA (pointer),Y
	INY
	LDA sprite_data+3
	STA (pointer),Y
	RTS

_abs16:
	PHA
	TYA
	BPL _neg16.1
	PLA
_neg16:
	EOR #$FF
	CLC
	ADC #1
	PHA
	TYA
	EOR #$FF
	ADC #0
	TAY
.1:
	PLA
	RTS

_sgn16:
	STY temp
	ORA temp
	BEQ .1
	TYA
	BMI .2
	LDA #0
	TAY
	LDA #1
	RTS

.2:	LDA #$FF
.1:	TAY
	RTS

_read16:
	JSR _read8
	PHA
	JSR _read8
	TAY
	PLA
	RTS

_read8:
	LDY #0
	LDA (read_pointer),Y
	INC read_pointer
	BNE .1
	INC read_pointer+1
.1:
	RTS

_peek8:
	STA pointer
	STY pointer+1
	LDY #0
	LDA (pointer),Y
	RTS

_peek16:
	STA pointer
	STY pointer+1
	LDY #0
	LDA (pointer),Y
	PHA
	INY
	LDA (pointer),Y
	TAY
	PLA
	RTS

	; temp2 contains left side (dividend)
	; temp contains right side (divisor)

	; 16-bit multiplication.
_mul16:
	PLA
	STA result
	PLA
	STA result+1
	PLA
	STA temp2+1
	PLA
	STA temp2
	LDA result+1
	PHA
	LDA result
	PHA
	LDA #0
	STA result
	STA result+1
	LDX #15
.1:
	LSR temp2+1
	ROR temp2
	BCC .2
	LDA result
	CLC
	ADC temp
	STA result
	LDA result+1
	ADC temp+1
	STA result+1
.2:	ASL temp
	ROL temp+1
	DEX
	BPL .1
	LDA result
	LDY result+1
	RTS

	; 16-bit signed modulo.
_mod16s:
	PLA
	STA result
	PLA
	STA result+1
	PLA
	STA temp2+1
	PLA
	STA temp2
	LDA result+1
	PHA
	LDA result
	PHA
	LDY temp2+1
	PHP
	BPL .1
	LDA temp2
	JSR _neg16
	STA temp2
	STY temp2+1
.1:
	LDY temp+1
	BPL .2
	LDA temp
	JSR _neg16
	STA temp
	STY temp+1
.2:
	JSR _mod16.1
	PLP
	BPL .3
	JMP _neg16
.3:
	RTS

	; 16-bit signed division.
_div16s:
	PLA
	STA result
	PLA
	STA result+1
	PLA
	STA temp2+1
	PLA
	STA temp2
	LDA result+1
	PHA
	LDA result
	PHA
	LDA temp+1
	EOR temp2+1
	PHP
	LDY temp2+1
	BPL .1
	LDA temp2
	JSR _neg16
	STA temp2
	STY temp2+1
.1:
	LDY temp+1
	BPL .2
	LDA temp
	JSR _neg16
	STA temp
	STY temp+1
.2:
	JSR _div16.1
	PLP
	BPL .3
	JMP _neg16
.3:
	RTS

_div16:
	PLA
	STA result
	PLA
	STA result+1
	PLA
	STA temp2+1
	PLA
	STA temp2
	LDA result+1
	PHA
	LDA result
	PHA
.1:
	LDA #0
	STA result
	STA result+1
	LDX #15
.2:
	ROL temp2
	ROL temp2+1
	ROL result
	ROL result+1
	LDA result
	SEC
	SBC temp
	STA result
	LDA result+1
	SBC temp+1
	STA result+1
	BCS .3
	LDA result
	ADC temp
	STA result
	LDA result+1
	ADC temp+1
	STA result+1
	CLC
.3:
	DEX
	BPL .2
	ROL temp2
	ROL temp2+1
	LDA temp2
	LDY temp2+1
	RTS

_mod16:
	PLA
	STA result
	PLA
	STA result+1
	PLA
	STA temp2+1
	PLA
	STA temp2
	LDA result+1
	PHA
	LDA result
	PHA
.1:
	LDA #0
	STA result
	STA result+1
	LDX #15
.2:
	ROL temp2
	ROL temp2+1
	ROL result
	ROL result+1
	LDA result
	SEC
	SBC temp
	STA result
	LDA result+1
	SBC temp+1
	STA result+1
	BCS .3
	LDA result
	ADC temp
	STA result
	LDA result+1
	ADC temp+1
	STA result+1
	CLC
.3:
	DEX
	BPL .2
	LDA result
	LDY result+1
	RTS

	; Random number generator.
	; From my game Mecha Eight.
random:
	LDA lfsr
	ORA lfsr+1
	BNE .0
	LDA #$11
	STA lfsr
	LDA #$78
	STA lfsr+1
.0:	LDA lfsr+1
	ROR A	
	ROR A		
	ROR A		
	EOR lfsr+1	
	STA temp
	LDA lfsr+1
	ROR A
	ROR A
	EOR temp
	STA temp
	LDA lfsr
	ASL A
	ASL A
	EOR temp
	ROL A
	ROR lfsr+1
	ROR lfsr
	LDA lfsr
	LDY lfsr+1
	RTS

sn76489_freq:
	STA temp
	STY temp+1
	STX temp2
	AND #$0f
	ORA temp2
	JSR BIOS_WRITE_PSG
	LDA temp+1
	ASL temp
	ROL A
	ASL temp
	ROL A
	ASL temp
	ROL A
	ASL temp
	ROL A
	AND #$3f	
	JMP BIOS_WRITE_PSG
	
sn76489_vol:
	STX temp2
	EOR #$ff
	AND #$0f
	ORA temp2
	JMP BIOS_WRITE_PSG

sn76489_control:
	AND #$0f
	ORA #$e0
	JMP BIOS_WRITE_PSG

vdp_generic_mode:
	SEI
	LDX #$00
	JSR WRTVDP
	LDA #$A2
	INX
	JSR WRTVDP
	LDA #$06	; $1800 for pattern table.
	INX
	JSR WRTVDP
	TYA
	INX		; for color table.
	JSR WRTVDP
	LDA temp+1
	INX		; for bitmap table.
	JSR WRTVDP
	LDA #$36	; $1b00 for sprite attribute table.
	INX
	JSR WRTVDP
	LDA #$07	; $3800 for sprites bitmaps.
	INX
	JSR WRTVDP
	LDA #$01
	INX
	JSR WRTVDP
	IF INCLUDE_FONT_DATA
	LDA #font_bitmaps
	LDY #font_bitmaps>>8
	STA temp
	STY temp+1
	LDA #$00
	STA pointer
	STA temp2
	LDA #$01
	STA pointer+1
	LDA #$03
	STA temp2+1
	ENDIF
	RTS

mode_0:
	LDA mode
	AND #$F7
	STA mode
	LDY #$ff	; $2000 for color table.
	LDA #$03	; $0000 for bitmaps
	STA temp+1
	LDA #$02
	JSR vdp_generic_mode
	JSR LDIRVM3
	SEI
	LDA #$f0
	STA temp
	LDA #$00
	STA pointer
	STA temp2
	LDY #$2000>>8
	STY pointer+1
	LDY #$1800>>8
	STY temp2+1
	JSR FILVRM
	CLI
	JSR cls
vdp_generic_sprites:
	LDA #$d1
	STA temp
	LDA #$00
	STA pointer
	STA temp2+1
	LDY #$1b00>>8
	STY pointer+1
	LDA #$80
	STA temp2
	SEI
	JSR FILVRM
	LDX #$7F
	LDA #$D1
.1:
	STA sprites,X
	DEX
	BPL .1
	LDA #$E2
	LDX #$01
	JSR WRTVDP
	CLI
	RTS

mode_1:
	LDA mode
	AND #$F7
	STA mode
	LDY #$ff	; $2000 for color table.
	LDA #$03	; $0000 for bitmaps
	STA temp+1
	LDA #$02
	JSR vdp_generic_mode
	LDA #$00
	STA temp
	STA pointer
	STA pointer+1
	STA temp2
	LDA #$18
	STA temp2+1
	JSR FILVRM
	CLI
	LDA #$f0
	STA temp
	LDA #$00
	STA pointer
	STA temp2
	LDY #$2000>>8
	STY pointer+1
	LDY #$1800>>8
	STY temp2+1
	SEI
	JSR FILVRM
	CLI
	LDA #$1800
	LDY #$1800>>8
	STA pointer
	STY pointer+1
.1:	SEI
	LDA pointer
	LDY pointer+1
	JSR SETWRT
	LDX #32
	LDY pointer
.2:
	TYA		; 2
	STA VDP_WRITE_DATA	; 4
	NOP		; 2
	NOP		; 2
	NOP		; 2
	INY		; 2
	DEX		; 2
	BNE .2		; 2/3/4
	CLI
	LDA pointer
	CLC
	ADC #32
	STA pointer
	BCC .1
	INC pointer+1
	LDA pointer+1
	CMP #$1B
	BNE .1
	JMP vdp_generic_sprites

mode_2:
	LDA mode
	ORA #$08
	STA mode
	LDY #$80	; $2000 for color table.
	LDA #$00	; $0000 for bitmaps
	STA temp+1
	JSR vdp_generic_mode
	JSR LDIRVM
	SEI
	LDA #$f0
	STA temp
	LDA #$00
	STA pointer
	STA temp2+1
	LDY #$2000>>8
	STY pointer+1
	LDA #$20
	STA temp2
	JSR FILVRM
	CLI
	JSR cls
	JMP vdp_generic_sprites

int_handler:
	PHA
	TXA
	PHA
	TYA
	PHA
	LDA VDP_READ_STATUS	; VDP interruption clear.
	STA vdp_status
	LDA #$1B00
	LDY #$1B00>>8
	JSR SETWRT
	LDA mode
	AND #$04
	BEQ .4
	LDX #0
.7:	LDA sprites,X	; 4
	STA VDP_WRITE_DATA	; 4
	NOP		; 2
	NOP		; 2
	INX		; 2
	CPX #$80	; 2
	BNE .7		; 2/3/4
	JMP .5

.4:	LDA flicker
	CLC
	ADC #4
	AND #$7f
	STA flicker
	TAX
	LDY #31
.6:
	LDA sprites,X
	STA VDP_WRITE_DATA	
	NOP
	NOP
	NOP
	NOP
	NOP
	INX
	LDA sprites,X
	STA VDP_WRITE_DATA
	NOP
	NOP
	NOP
	NOP
	NOP
	INX
	LDA sprites,X
	STA VDP_WRITE_DATA
	NOP
	NOP
	NOP
	NOP
	NOP
	INX
	LDA sprites,X
	STA VDP_WRITE_DATA
	TXA
	CLC
	ADC #25
	AND #$7f
	TAX
	DEY
	BPL .6
.5:
	JSR BIOS_READ_CONTROLLERS

	LDX joy1_dir
	LDA joy1_buttons
	JSR convert_joystick
	STA joy1_data

	LDX joy2_dir
	LDA joy2_buttons
	LSR A
	LSR A
	JSR convert_joystick
	STA joy2_data

	LDX #1
	LDA $18
	CMP #$0C
	BEQ .11
	INX
	LDA $19
	CMP #$30
	BEQ .11
	INX
	CMP #$60
	BEQ .11
	INX
	CMP #$28
	BEQ .11
	INX
	CMP #$48
	BEQ .11
	INX
	CMP #$50
	BEQ .11
	INX
	LDA $1B
	CMP #$06
	BEQ .11
	INX
	CMP #$42
	BEQ .11
	INX
	CMP #$22
	BEQ .11
	LDX #0
	CMP #$12
	BEQ .11
	LDX #11
	CMP #$09
	BEQ .11
	LDA $19
	LDX #10
	CMP #$09
	BEQ .11
	LDX #$0f
.11:	STX key1_data

    if CVBASIC_MUSIC_PLAYER
	LDA music_mode
	BEQ .10
	JSR music_hardware
.10:
    endif
	INC frame
	BNE .8
	INC frame+1
.8:
	INC lfsr	; Make LFSR more random
	INC lfsr
	INC lfsr
    if CVBASIC_MUSIC_PLAYER
	LDA music_mode
	BEQ .9
	JSR music_generate
.9:
    endif
	; This is like saving extra registers, because these
	; are used by the compiled code, and we don't want
	; any reentrancy.
	LDA temp+0
	PHA
	LDA temp+1
	PHA
	LDA temp+2
	PHA
	LDA temp+3
	PHA
	LDA temp+4
	PHA
	LDA temp+5
	PHA
	LDA temp+6
	PHA
	LDA temp+7
	PHA
	;CVBASIC MARK DON'T CHANGE
	PLA
	STA temp+7
	PLA
	STA temp+6
	PLA
	STA temp+5
	PLA
	STA temp+4
	PLA
	STA temp+3
	PLA
	STA temp+2
	PLA
	STA temp+1
	PLA
	STA temp+0

	PLA
	TAY
	PLA
	TAX
	PLA
	RTI

convert_joystick:
	ROR A
	ROR A
	ROR A
	AND #$C0
	TAY
	TXA
	BEQ .1
	AND #$0F
	TAX
;	LDA FRAME
;	AND #1
;	BEQ .2
	TYA
	ORA joystick_table,X
	RTS
;.2:
;	TYA
;	ORA joystick_table+16,X
;	RTS

.1:	TYA
	RTS

joystick_table:
	DB $04,$04,$06,$06,$02,$02,$03,$03
	DB $01,$01,$09,$09,$08,$08,$0C,$0C

;	DB $0C,$04,$04,$06,$06,$02,$02,$03
;	DB $03,$01,$01,$09,$09,$08,$08,$0C

wait:
	LDA frame
.1:	CMP frame
	BEQ .1
	RTS

music_init:
	LDA #$9f
	JSR BIOS_WRITE_PSG	
	LDA #$bf
	JSR BIOS_WRITE_PSG	
	LDA #$df
	JSR BIOS_WRITE_PSG	
	LDA #$ff
	JSR BIOS_WRITE_PSG
    if CVBASIC_MUSIC_PLAYER
    else	
	RTS
    endif

    if CVBASIC_MUSIC_PLAYER
	LDA #$ff
	STA audio_vol4hw
	LDA #$00
	STA audio_control
	LDA #music_silence
	LDY #music_silence>>8
	;
	; Play music.
	; YA = Pointer to music.
	;
music_play:
	SEI
	STA music_pointer
	STY music_pointer+1
	LDY #0
	STY music_note_counter
	LDA (music_pointer),Y
	STA music_timing
	INY
	STY music_playing
	INC music_pointer
	BNE $+4
	INC music_pointer+1
	LDA music_pointer
	LDY music_pointer+1
	STA music_start
	STY music_start+1
	CLI
	RTS

	;
	; Generates music
	;
music_generate:
	LDA #0
	STA audio_vol1
	STA audio_vol2
	STA audio_vol3
	LDA #$FF
	STA audio_vol4hw
	LDA music_note_counter
	BEQ .1
	JMP .2
.1:
	LDY #0
	LDA (music_pointer),Y
	CMP #$fe	; End of music?
	BNE .3		; No, jump.
	LDA #0		; Keep at same place.
	STA music_playing
	RTS

.3:	CMP #$fd	; Repeat music?
	BNE .4
	LDA music_start
	LDY music_start+1
	STA music_pointer
	STY music_pointer+1
	JMP .1

.4:	LDA music_timing
	AND #$3f	; Restart note time.
	STA music_note_counter

	LDA (music_pointer),Y
	CMP #$3F	; Sustain?
	BEQ .5
	AND #$C0
	STA music_instrument_1
	LDA (music_pointer),Y
	AND #$3F
	ASL A
	STA music_note_1
	LDA #0
	STA music_counter_1
.5:
	INY
	LDA (music_pointer),Y
	CMP #$3F	; Sustain?
	BEQ .6
	AND #$C0
	STA music_instrument_2
	LDA (music_pointer),Y
	AND #$3F
	ASL A
	STA music_note_2
	LDA #0
	STA music_counter_2
.6:
	INY
	LDA (music_pointer),Y
	CMP #$3F	; Sustain?
	BEQ .7
	AND #$C0
	STA music_instrument_3
	LDA (music_pointer),Y
	AND #$3F
	ASL A
	STA music_note_3
	LDA #0
	STA music_counter_3
.7:
	INY
	LDA (music_pointer),Y
	STA music_drum
	LDA #0	
	STA music_counter_4
	LDA music_pointer
	CLC
	ADC #4
	STA music_pointer
	LDA music_pointer+1
	ADC #0
	STA music_pointer+1
.2:
	LDY music_note_1
	BEQ .8
	LDA music_instrument_1
	LDX music_counter_1
	JSR music_note2freq
	STA audio_freq1
	STY audio_freq1+1
	STX audio_vol1
.8:
	LDY music_note_2
	BEQ .9
	LDA music_instrument_2
	LDX music_counter_2
	JSR music_note2freq
	STA audio_freq2
	STY audio_freq2+1
	STX audio_vol2
.9:
	LDY music_note_3
	BEQ .10
	LDA music_instrument_3
	LDX music_counter_3
	JSR music_note2freq
	STA audio_freq3
	STY audio_freq3+1
	STX audio_vol3
.10:
	LDA music_drum
	BEQ .11
	CMP #1		; 1 - Long drum.
	BNE .12
	LDA music_counter_4
	CMP #3
	BCS .11
.15:
	LDA #$ec
	STA audio_noise
	LDA #$f5
	STA audio_vol4hw
	JMP .11

.12:	CMP #2		; 2 - Short drum.
	BNE .14
	LDA music_counter_4
	CMP #0
	BNE .11
	LDA #$ed
	STA audio_noise
	LDA #$F5
	STA audio_vol4hw
	JMP .11

.14:	;CMP #3		; 3 - Roll.
	;BNE
	LDA music_counter_4
	CMP #2
	BCC .15
	ASL A
	SEC
	SBC music_timing
	BCC .11
	CMP #4
	BCC .15
.11:
	LDX music_counter_1
	INX
	CPX #$18
	BNE $+4
	LDX #$10
	STX music_counter_1

	LDX music_counter_2
	INX
	CPX #$18
	BNE $+4
	LDX #$10
	STX music_counter_2

	LDX music_counter_3
	INX
	CPX #$18
	BNE $+4
	LDX #$10
	STX music_counter_3

	INC music_counter_4
	DEC music_note_counter
	RTS

music_flute:
	LDA music_notes_table,Y
	CLC
	ADC .2,X
	PHA
	LDA music_notes_table+1,Y
	ADC #0
	TAY
	LDA .1,X
	TAX
	PLA
	RTS

.1:
        db 10,12,13,13,12,12,12,12
        db 11,11,11,11,10,10,10,10
        db 11,11,11,11,10,10,10,10

.2:
	db 0,0,0,0,0,1,1,1
	db 0,1,1,1,0,1,1,1
	db 0,1,1,1,0,1,1,1

	;
	; Converts note to frequency.
	; Input:
	;   A = Instrument.
	;   Y = Note (1-62)
	;   X = Instrument counter.
	; Output:
	;   YA = Frequency.
	;   X = Volume.
	;
music_note2freq:
	CMP #$40
	BCC music_piano
	BEQ music_clarinet
	CMP #$80
	BEQ music_flute
	;
	; Bass instrument
	; 
music_bass:
	LDA music_notes_table,Y
	ASL A
	PHA
	LDA music_notes_table+1,Y
	ROL A
	TAY
	LDA .1,X
	TAX
	PLA
	RTS

.1:
	db 13,13,12,12,11,11,10,10
	db 9,9,8,8,7,7,6,6
	db 5,5,4,4,3,3,2,2

music_piano:
	LDA music_notes_table,Y
	PHA
	LDA music_notes_table+1,Y
	TAY
	LDA .1,X
	TAX
	PLA
	RTS

.1:	db 12,11,11,10,10,9,9,8
	db 8,7,7,6,6,5,5,4
	db 4,4,5,5,4,4,3,3

music_clarinet:
	LDA music_notes_table,Y
	CLC
	ADC .2,X
	PHA
	LDA .2,X
	BMI .3
	LDA #$00
	DB $2C
.3:	LDA #$ff
	ADC music_notes_table+1,Y
	LSR A
	TAY
	LDA .1,X
	TAX
	PLA
	ROR A
	RTS

.1:
        db 13,14,14,13,13,12,12,12
        db 11,11,11,11,12,12,12,12
        db 11,11,11,11,12,12,12,12

.2:
	db 0,0,0,0,-1,-2,-1,0
	db 1,2,1,0,-1,-2,-1,0
	db 1,2,1,0,-1,-2,-1,0

	;
	; Musical notes table.
	;
music_notes_table:
	; Silence - 0
	dw 0
	; Values for 2.00 mhz.
	; 2nd octave - Index 1
	dw 956,902,851,804,758,716,676,638,602,568,536,506
	; 3rd octave - Index 13
	dw 478,451,426,402,379,358,338,319,301,284,268,253
	; 4th octave - Index 25
	dw 239,225,213,201,190,179,169,159,150,142,134,127
	; 5th octave - Index 37
	dw 119,113,106,100,95,89,84,80,75,71,67,63
	; 6th octave - Index 49
	dw 60,56,53,50,47,45,42,40,38,36,34,32
	; 7th octave - Index 61
	dw 30,28,27

music_hardware:
	LDA music_mode
	CMP #4		; PLAY SIMPLE?
	BCC .7		; Yes, jump.
	LDA audio_vol2
	BNE .7
	LDA audio_vol3
	BEQ .7
	STA audio_vol2
	LDA #0
	STA audio_vol3
	LDA audio_freq3
	LDY audio_freq3+1
	STA audio_freq2
	STY audio_freq2+1
.7:
	LDA audio_freq1+1
	CMP #$04
	LDA #$9F
	BCS .1
	LDA audio_freq1
	AND #$0F
	ORA #$80
	JSR BIOS_WRITE_PSG
	LDA audio_freq1+1
	ASL audio_freq1
	ROL A
	ASL audio_freq1
	ROL A
	ASL audio_freq1
	ROL A
	ASL audio_freq1
	ROL A
	JSR BIOS_WRITE_PSG
	LDX audio_vol1
	LDA ay2sn,X
	ORA #$90
.1:	JSR BIOS_WRITE_PSG

	LDA audio_freq2+1
	CMP #$04
	LDA #$BF
	BCS .2
	LDA audio_freq2
	AND #$0F
	ORA #$A0
	JSR BIOS_WRITE_PSG
	LDA audio_freq2+1
	ASL audio_freq2
	ROL A
	ASL audio_freq2
	ROL A
	ASL audio_freq2
	ROL A
	ASL audio_freq2
	ROL A
	JSR BIOS_WRITE_PSG
	LDX audio_vol2
	LDA ay2sn,X
	ORA #$b0
.2:	JSR BIOS_WRITE_PSG

	LDA music_mode
	CMP #4		; PLAY SIMPLE?
	BCC .6		; Yes, jump.

	LDA audio_freq3+1
	CMP #$04
	LDA #$DF
	BCS .3
	LDA audio_freq3
	AND #$0F
	ORA #$C0
	JSR BIOS_WRITE_PSG
	LDA audio_freq3+1
	ASL audio_freq3
	ROL A
	ASL audio_freq3
	ROL A
	ASL audio_freq3
	ROL A
	ASL audio_freq3
	ROL A
	JSR BIOS_WRITE_PSG
	LDX audio_vol3
	LDA ay2sn,X
	ORA #$D0
.3:	JSR BIOS_WRITE_PSG

.6:	LDA music_mode
	LSR A		; NO DRUMS?
	BCC .8
	LDA audio_vol4hw
	CMP #$ff
	BEQ .4
	LDA audio_noise
	CMP audio_control
	BEQ .4
	STA audio_control
	JSR BIOS_WRITE_PSG
.4:	LDA audio_vol4hw
	JSR BIOS_WRITE_PSG
.8:
	RTS

        ;
        ; Converts AY-3-8910 volume to SN76489
        ;
ay2sn:
        db $0f,$0f,$0f,$0e,$0e,$0e,$0d,$0b,$0a,$08,$07,$05,$04,$03,$01,$00

music_silence:
	db 8
	db 0,0,0,0
	db -2
    endif

    if CVBASIC_COMPRESSION
define_char_unpack:
	lda #0
	sta pointer+1
	lda pointer
	asl a
	rol pointer+1
	asl a
	rol pointer+1
	asl a
	rol pointer+1
	sta pointer
	lda mode
	and #$08
	beq unpack3
	bne unpack

define_color_unpack:
	lda #4
	sta pointer+1
	lda pointer
	asl a
	rol pointer+1
	asl a
	rol pointer+1
	asl a
	rol pointer+1
	sta pointer
unpack3:
	jsr .1
	jsr .1
.1:	lda pointer
	pha
	lda pointer+1
	pha
	lda temp
	pha
	lda temp+1
	pha
	jsr unpack
	pla
	sta temp+1
	pla
	sta temp
	pla
	clc
	adc #8
	sta pointer+1
	pla
	sta pointer
	rts

        ;
        ; Pletter-0.5c decompressor (XL2S Entertainment & Team Bomba)
        ; Ported from Z80 original
	; temp = Pointer to source data
	; pointer = Pointer to target VRAM
	; temp2
	; temp2+1
	; result
	; result+1
	; pletter_off
	; pletter_off+1
	;
unpack:
	; Initialization
	ldy #0
	sty temp2
	lda (temp),y
	inc temp
	bne $+4
	inc temp+1
	asl a
	rol temp2
	adc #1
	asl a
	rol temp2
	asl a
	sta pletter_bit
	rol temp2
	rol temp2
	lda #.modes
	adc temp2
	sta temp2
	lda #.modes>>8
	adc #0
	sta temp2+1
	lda (temp2),y
	tax
	iny
	lda (temp2),y
	stx temp2	; IX (temp2)
	sta temp2+1
	lda pletter_bit
.literal:
	sta pletter_bit
	ldy #0
	lda (temp),y
	inc temp
	bne $+4
	inc temp+1
	tax
	lda pointer
	ldy pointer+1
	sei
	jsr WRTVRM
	cli
	inc pointer
	bne $+4
	inc pointer+1
	lda pletter_bit
.loop:
	asl a
	bne $+5
	jsr .getbit
	bcc .literal

	; Compressed data
	ldx #1
	stx result
	dex
	stx result+1
.getlen:
	asl a
	bne $+5
	jsr .getbit
	bcc .lenok
.lus:	asl a
	bne $+5
	jsr .getbit
	rol result
	rol result+1
	bcc $+3
	rts
	asl a
	bne $+5
	jsr .getbit
	bcc .lenok
	asl a
	bne $+5
	jsr .getbit
	rol result
	rol result+1
	bcc $+3
	rts
	asl a
	bne $+5
	jsr .getbit
	bcs .lus
.lenok:
	inc result
	bne $+4
	inc result+1
	sta pletter_bit
	ldy #0
	sty pletter_off+1
	lda (temp),y
	inc temp
	bne $+4
	inc temp+1
	sta pletter_off
	lda pletter_off
	bpl .offsok
	lda pletter_bit
	jmp (temp2)
	
.mode6:
	asl a
	bne $+5
	jsr .getbit
	rol pletter_off+1
.mode5:
	asl a
	bne $+5
	jsr .getbit
	rol pletter_off+1
.mode4:
	asl a
	bne $+5
	jsr .getbit
	rol pletter_off+1
.mode3:
	asl a
	bne $+5
	jsr .getbit
	rol pletter_off+1
.mode2:
	asl a
	bne $+5
	jsr .getbit
	rol pletter_off+1
	asl a
	bne $+5
	jsr .getbit
	sta pletter_bit
	bcc .offsok
	inc pletter_off+1
	lda pletter_off
	and #$7f
	sta pletter_off
.offsok:
	inc pletter_off
	bne $+4
	inc pletter_off+1

	lda result
	beq $+4
	inc result+1

	lda pointer
	sec
	sbc pletter_off
	sta pletter_off
	lda pointer+1
	sbc pletter_off+1
	sta pletter_off+1
.loop2:
	sei
	lda pletter_off
	ldy pletter_off+1
	jsr RDVRM
	tax
	lda pointer
	ldy pointer+1
	jsr WRTVRM
	cli
	inc pletter_off
	bne $+4
	inc pletter_off+1
	inc pointer
	bne $+4
	inc pointer+1
	dec result
	bne .loop2
	dec result+1
	bne .loop2

	lda pletter_bit
	jmp .loop

.getbit:
	ldy #0
	lda (temp),y
	inc temp
	bne $+4
	inc temp+1
	rol a
	rts

.modes:
	dw .offsok
	dw .mode2
	dw .mode3
	dw .mode4
	dw .mode5
	dw .mode6
    endif

	IF INCLUDE_FONT_DATA
	; Required for Creativision because it doesn't provide an ASCII charset.
	;
        ; My personal font for TMS9928.
        ;
        ; Patterned after the TMS9928 programming manual 6x8 letters
        ; with better lowercase letters, also I made a proper
        ; AT sign.
        ;
font_bitmaps:
        db $00,$00,$00,$00,$00,$00,$00,$00      ; $20 space
        db $20,$20,$20,$20,$20,$00,$20,$00      ; $21 !
        db $50,$50,$50,$00,$00,$00,$00,$00      ; $22 "
        db $50,$50,$f8,$50,$f8,$50,$50,$00      ; $23 #
        db $20,$78,$a0,$70,$28,$f0,$20,$00      ; $24 $
        db $c0,$c8,$10,$20,$40,$98,$18,$00      ; $25 %
        db $40,$a0,$40,$a0,$a8,$90,$68,$00      ; $26 &
        db $60,$20,$40,$00,$00,$00,$00,$00      ; $27 '
        db $10,$20,$40,$40,$40,$20,$10,$00      ; $28 (
        db $40,$20,$10,$10,$10,$20,$40,$00      ; $29 )
        db $00,$a8,$70,$20,$70,$a8,$00,$00      ; $2a *
        db $00,$20,$20,$f8,$20,$20,$00,$00      ; $2b +
        db $00,$00,$00,$00,$00,$60,$20,$40      ; $2c ,
        db $00,$00,$00,$fc,$00,$00,$00,$00      ; $2d -
        db $00,$00,$00,$00,$00,$00,$60,$00      ; $2e .
        db $00,$08,$10,$20,$40,$80,$00,$00      ; $2f /
        db $70,$88,$98,$a8,$c8,$88,$70,$00      ; $30 0
        db $20,$60,$20,$20,$20,$20,$f8,$00      ; $31 1
        db $70,$88,$08,$10,$60,$80,$f8,$00      ; $32 2
        db $70,$88,$08,$30,$08,$88,$70,$00      ; $33 3
        db $30,$50,$90,$90,$f8,$10,$10,$00      ; $34 4
        db $f8,$80,$f0,$08,$08,$08,$f0,$00      ; $35 5
        db $30,$40,$80,$f0,$88,$88,$70,$00      ; $36 6
        db $f8,$08,$10,$20,$20,$20,$20,$00      ; $37 7
        db $70,$88,$88,$70,$88,$88,$70,$00      ; $38 8
        db $70,$88,$88,$78,$08,$10,$60,$00      ; $39 9
        db $00,$00,$00,$60,$00,$60,$00,$00      ; $3a :
        db $00,$00,$00,$60,$00,$60,$20,$40      ; $3b ;
        db $10,$20,$40,$80,$40,$20,$10,$00      ; $3c <
        db $00,$00,$f8,$00,$f8,$00,$00,$00      ; $3d =
        db $08,$04,$02,$01,$02,$04,$08,$00      ; $3e >
        db $70,$88,$08,$10,$20,$00,$20,$00      ; $3f ?
        db $70,$88,$98,$a8,$98,$80,$70,$00      ; $40 @
        db $20,$50,$88,$88,$f8,$88,$88,$00      ; $41 A
        db $f0,$88,$88,$f0,$88,$88,$f0,$00      ; $42 B
        db $70,$88,$80,$80,$80,$88,$70,$00      ; $43 C
        db $f0,$88,$88,$88,$88,$88,$f0,$00      ; $44 D
        db $f8,$80,$80,$f0,$80,$80,$f8,$00      ; $45 E
        db $f8,$80,$80,$f0,$80,$80,$80,$00      ; $46 F
        db $70,$88,$80,$b8,$88,$88,$70,$00      ; $47 G
        db $88,$88,$88,$f8,$88,$88,$88,$00      ; $48 H
        db $70,$20,$20,$20,$20,$20,$70,$00      ; $49 I
        db $08,$08,$08,$08,$88,$88,$70,$00      ; $4A J
        db $88,$90,$a0,$c0,$a0,$90,$88,$00      ; $4B K
        db $80,$80,$80,$80,$80,$80,$f8,$00      ; $4C L
        db $88,$d8,$a8,$a8,$88,$88,$88,$00      ; $4D M
        db $88,$c8,$c8,$a8,$98,$98,$88,$00      ; $4E N
        db $70,$88,$88,$88,$88,$88,$70,$00      ; $4F O
        db $f0,$88,$88,$f0,$80,$80,$80,$00      ; $50 P
        db $70,$88,$88,$88,$88,$a8,$90,$68      ; $51 Q
        db $f0,$88,$88,$f0,$a0,$90,$88,$00      ; $52 R
        db $70,$88,$80,$70,$08,$88,$70,$00      ; $53 S
        db $f8,$20,$20,$20,$20,$20,$20,$00      ; $54 T
        db $88,$88,$88,$88,$88,$88,$70,$00      ; $55 U
        db $88,$88,$88,$88,$50,$50,$20,$00      ; $56 V
        db $88,$88,$88,$a8,$a8,$d8,$88,$00      ; $57 W
        db $88,$88,$50,$20,$50,$88,$88,$00      ; $58 X
        db $88,$88,$88,$70,$20,$20,$20,$00      ; $59 Y
        db $f8,$08,$10,$20,$40,$80,$f8,$00      ; $5A Z
        db $78,$60,$60,$60,$60,$60,$78,$00      ; $5B [
        db $00,$80,$40,$20,$10,$08,$00,$00      ; $5C \
        db $F0,$30,$30,$30,$30,$30,$F0,$00      ; $5D ]
        db $20,$50,$88,$00,$00,$00,$00,$00      ; $5E 
        db $00,$00,$00,$00,$00,$00,$f8,$00      ; $5F _
        db $40,$20,$10,$00,$00,$00,$00,$00      ; $60 
        db $00,$00,$68,$98,$88,$98,$68,$00      ; $61 a
        db $80,$80,$f0,$88,$88,$88,$f0,$00      ; $62 b
        db $00,$00,$78,$80,$80,$80,$78,$00      ; $63 c
        db $08,$08,$68,$98,$88,$98,$68,$00      ; $64 d
        db $00,$00,$70,$88,$f8,$80,$70,$00      ; $65 e
        db $30,$48,$40,$e0,$40,$40,$40,$00      ; $66 f
        db $00,$00,$78,$88,$88,$78,$08,$70      ; $67 g
        db $80,$80,$f0,$88,$88,$88,$88,$00      ; $68 h
        db $20,$00,$60,$20,$20,$20,$70,$00      ; $69 i
        db $08,$00,$18,$08,$88,$88,$70,$00      ; $6a j
        db $80,$80,$88,$90,$e0,$90,$88,$00      ; $6b k
        db $60,$20,$20,$20,$20,$20,$70,$00      ; $6c l
        db $00,$00,$d0,$a8,$a8,$a8,$a8,$00      ; $6d m
        db $00,$00,$b0,$c8,$88,$88,$88,$00      ; $6e n
        db $00,$00,$70,$88,$88,$88,$70,$00      ; $6f o
        db $00,$00,$f0,$88,$88,$88,$f0,$80      ; $70 p
        db $00,$00,$78,$88,$88,$88,$78,$08      ; $71 q
        db $00,$00,$b8,$c0,$80,$80,$80,$00      ; $72 r
        db $00,$00,$78,$80,$70,$08,$f0,$00      ; $73 s
        db $20,$20,$f8,$20,$20,$20,$20,$00      ; $74 t
        db $00,$00,$88,$88,$88,$98,$68,$00      ; $75 u
        db $00,$00,$88,$88,$88,$50,$20,$00      ; $76 v
        db $00,$00,$88,$a8,$a8,$a8,$50,$00      ; $77 w
        db $00,$00,$88,$50,$20,$50,$88,$00      ; $78 x
        db $00,$00,$88,$88,$98,$68,$08,$70      ; $79 y
        db $00,$00,$f8,$10,$20,$40,$f8,$00      ; $7a z
        db $18,$20,$20,$40,$20,$20,$18,$00      ; $7b {
        db $20,$20,$20,$20,$20,$20,$20,$00      ; $7c |
        db $c0,$20,$20,$10,$20,$20,$c0,$00      ; $7d } 
        db $00,$00,$40,$a8,$10,$00,$00,$00      ; $7e
        db $70,$70,$20,$f8,$20,$70,$50,$00      ; $7f

	ENDIF

START:
	SEI
	CLD

	LDA #$00
	TAX
.1:	STA $0100,X
	STA $0200,X
	STA $0300,X
	INX
	BNE .1

	LDX #STACK
	TXS
	LDA VDP_READ_STATUS
	LDA #$82
	LDX #$01
	JSR WRTVDP
	LDA VDP_READ_STATUS
	LDA #$82
	LDX #$01
	JSR WRTVDP

	JSR music_init

	JSR mode_0

	LDA #$00
	STA joy1_data
	STA joy2_data
	LDA #$0F
	STA key1_data
	STA key2_data


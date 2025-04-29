	;
	; CVBasic prologue (BASIC compiler for Colecovision and other consoles)
	;
	; by Oscar Toledo G.
	; https://nanochess.org/
	;
	; Creation date: Feb/27/2024.
	; Revision date: Feb/29/2024. Turns off sound. Now it reads the controllers.
	;                             Added 16-bit multiply, division, modulo, and abs.
	;                             Added random generator. Added sound routines.
	; Revision date: Mar/03/2024. Removed fname directive to use gasm80.
	; Revision date: Mar/05/2024. Added support for Sega SG1000.
	; Revision date: Mar/06/2024. Added ENASCR, DISSCR, and CPYBLK.
	; Revision date: Mar/08/2024. Added modes 0, 1 and 2.
	; Revision date: Mar/12/2024. Added support for MSX.
	; Revision date: Mar/14/2024. Added _sgn16.
	; Revision date: Mar/15/2024. Added upper 16k enable for MSX.
	; Revision date: Apr/11/2024. Added support for formatting numbers. Added
	;                             support for Super Game Module.
	; Revision date: Apr/13/2024. Saved bytes in SG-1000 ROMs. Faster LDIRVM.
	;                             Shorter mode setting subroutines.
	; Revision date: Apr/26/2024. Interruption handler saves current bank.
	; Revision date: Apr/27/2024. Music player now supports bank switching.
	; Revision date: May/17/2024. Added delay for SG1000 and SC3000 controller
	;                             support with keyboard (code by SiRioKD)
	; Revision date: Jun/04/2024. SGM supported deleted NTSC flag.
	; Revision date: Jun/07/2024. Keys 0-9, = and - emulate keypad in MSX.
	; Revision date: Jun/17/2024. Added SVI-328 support.
	; Revision date: Aug/01/2024. Added Sord M5 support.
	; Revision date: Aug/02/2024. PSG label now defined by CVBasic. Added Memotech
	;                             support.
	; Revision date: Aug/08/2024. Added Soundic/Hanimex Pencil II support.
	; Revision date: Aug/15/2024. Added support for Tatung Einstein. Added support
	;                             for Casio PV-2000.
	; Revision date: Aug/21/2024. Added keypad support for Memotech, Tatung Einstein,
	;                             and Casio PV-2000.
	; Revision date: Aug/30/2024. Changed mode bit to bit 3 (avoids collision
	;                             with flicker flag).
	; Revision date: Oct/15/2024. Added LDIRMV. Solved bug where asterisk and number
	;                             keys values were inverted.
	; Revision date: Nov/12/2024. Saves the VDP status.
	;


INCLUDE_FONT_DATA: equ 0

JOYSEL:	equ $c0
KEYSEL:	equ $80

JOY1:   equ $fc-$20*(SG1000+SMS)
JOY2:   equ $ff-$22*(SG1000+SMS)

    if COLECO
	org $8000
      if PENCIL
        db "COPYRIGHT SOUNDIC"
	jp START
	jp nmi_handler
	jp 0	; rst $08
	jp 0	; rst $10
	jp 0	; rst $18
	jp 0	; rst $20
	jp 0	; rst $28
	jp 0	; rst $30
	jp 0	; rst $38
	dw $0000
	dw $0000
	dw $0000
	dw $0000
	db "CVBASIC!POWERED BY!2024!"
      else
	db $55,$aa
	dw 0
	dw 0
	dw 0
	dw 0
	dw START

	jp 0	; rst $08
	jp 0	; rst $10
	jp 0	; rst $18
	jp 0	; rst $20
	jp 0	; rst $28
	jp 0	; rst $30
	jp 0	; rst $38

	jp nmi_handler
      endif
    endif
    if SG1000+SMS+SVI
	org $0000
	di
	ld sp,STACK
	jp START
	db $ff
	jp 0
	db $ff,$ff,$ff,$ff,$ff
	jp 0
	db $ff,$ff,$ff,$ff,$ff
	jp 0
	db $ff,$ff,$ff,$ff,$ff
	jp 0
	db $ff,$ff,$ff,$ff,$ff
	jp 0
	db $ff,$ff,$ff,$ff,$ff
	jp 0
	db $ff,$ff,$ff,$ff,$ff
	jp nmi_handler	; It should be called int_handler.
    endif
    if MSX
	ORG $4000
	db "AB"
	dw START
	dw $0000
	dw $0000
	dw $0000
	dw $0000

WRTPSG:	equ $0093
RDPSG:	equ $0096

    endif
    if SORD
	ORG $2000
	db $02		; Avoid checking $4000
	dw START	; Start address.
	dw $002e	; Prestart address (just point to RET in BIOS).
    endif
    if PV2000
	ORG $C000
	jp START
    endif

    if MEMOTECH
      if CPM
	org $0100
      else
	org $40fc
	dw rom_start
	dw rom_end-rom_start
      endif
rom_start:
	jp START
	db 0,0,0,0,0
	dw nmi_handler
	dw null_vector
	dw null_vector
	dw rom_start

null_vector:
	ei
	reti
    endif
    if EINSTEIN
	org $0100
rom_start:
	jp START
	db 0,0,0,0,0
	dw $0000
	dw $0000
	dw $0000
	dw $0000
    endif
    if NABU
      if CPM
	org $0100
      else
	org $140d
      endif
	nop
	nop
	nop
	jp START
        times $100-($&255) db $ff
nabu_int:
	dw null_vector
	dw null_vector
	dw keyboard_handler
	dw nmi_handler
	dw null_vector
	dw null_vector
	dw null_vector
	dw null_vector

null_vector:
	ei
	reti
    endif

    if SVI
WRTPSG:	
	out ($88),a
	push af
	ld a,e
	out ($8c),a
	pop af
	ret

RDPSG:
	out ($88),a
	push af
	pop af
	in a,($90)
	ret
    endif

    if EINSTEIN
WRTPSG:	
	out ($02),a
	push af
	ld a,e
	out ($03),a
	pop af
	ret

RDPSG:
	out ($02),a
	push af
	pop af
	in a,($02)
	ret
    endif

    if NABU
WRTPSG:	
	out ($41),a
	push af
	ld a,e
	out ($40),a
	pop af
	ret

RDPSG:
	out ($41),a
	push af
	pop af
	in a,($40)
	ret
    endif

    if PV2000
	; The Casio PV2000 has the VDP ports mapped into main memory (MREQ)
WRTVDP:
	ld a,b
	ld (VDP+1),a
	ld a,c
	or $80
	ld (VDP+1),a
	ret

SETWRT:
	ld a,l
	ld (VDP+1),a
	ld a,h
	or $40
	ld (VDP+1),a
	ret

SETRD:
	ld a,l
	ld (VDP+1),a
	ld a,h
        and $3f
	ld (VDP+1),a
	ret

WRTVRM:
	push af
	call SETWRT
	pop af
	ld (VDP),a
	ret

RDVRM:
        push af
        call SETRD
        pop af
        ex (sp),hl
        ex (sp),hl
        ld a,(VDP)
        ret

FILVRM:
	push af
	call SETWRT
	pop af
	dec bc		; T-states (normal / M1)
.1:	ld (VDP),a	; 13 14
	dec bc		;  6  7
	bit 7,b		;  8 10
	jp z,.1		; 10 11
			; -- --
			; 37 42
	ret

LDIRMV:
	ex de,hl
	call SETRD
	ex (sp),hl
	ex (sp),hl
.1:
	ld a,(VDP)
	ld (de),a
	inc de
	dec bc
	ld a,b
	or c
	jp nz,.1
	ret

LDIRVM:
        EX DE,HL
        CALL SETWRT
        EX DE,HL
        DEC BC
        INC C
        LD A,B
        LD B,C
        INC A
	LD C,A
.1:
	LD A,(HL)	;  7  8
	LD (VDP),A	; 13 14
	INC HL		;  6  7
	DJNZ .1		; 13 14
        DEC C		;  4  5
        JP NZ,.1	; 10 11
        RET
    else
	; Normal platforms with VDP connected via ports (IORQ)
WRTVDP:
	ld a,b
	out (VDP+1),a
	ld a,c
	or $80
	out (VDP+1),a
	ret

SETWRT:
	ld a,l
	out (VDP+1),a
	ld a,h
	or $40
	out (VDP+1),a
	ret

SETRD:
	ld a,l
	out (VDP+1),a
	ld a,h
        and $3f
	out (VDP+1),a
	ret

WRTVRM:
	push af
	call SETWRT
	pop af
	out (VDP),a
	ret

    if SG1000+SMS
	db $ff,$ff,$ff,$ff,$ff,$ff,$ff,$ff

	; Located at $0066
	ei		; NMI handler (pause button)
	retn
    endif

RDVRM:
        push af
        call SETRD
        pop af
        ex (sp),hl
        ex (sp),hl
        in a,(VDPR)
        ret

; Read the status register from VDP - data returned in a (visrealm)
RDVST:
    in a,(VDPR+1)
    ld l, a
    ret

FILVRM:
	push af
	call SETWRT
	pop af
	dec bc		; T-states (normal / M1)
.1:	out (VDP),a	; 11 12
	dec bc		;  6  7
	bit 7,b		;  8 10
	jp z,.1		; 10 11
			; -- --
			; 35 40
	ret

LDIRMV:
	ex de,hl
	call SETRD
	ex (sp),hl
	ex (sp),hl
.1:
	in a,(VDP)
	ld (de),a
    if SORD+SMS
	nop	
    endif
    if SG1000+MEMOTECH+EINSTEIN
	nop	; SG1000 is 3.58 mhz, but SC3000 is 4 mhz.
	nop
    endif
	inc de
	dec bc
	ld a,b
	or c
	jp nz,.1
	ret

LDIRVM:
        ex de,hl
        call SETWRT
        ex de,hl
        dec bc
        inc c
        ld a,b
        ld b,c
        inc a
        ld c,VDP
.1:
    if SORD+SMS
	nop	
    endif
    if SG1000+MEMOTECH+EINSTEIN
	nop	; SG1000 is 3.58 mhz, but SC3000 is 4 mhz.
	nop
    endif
	outi
        jp nz,.1
        dec a
        jp nz,.1
        ret
    endif

    if SMS
    else
LDIRVM3:
	call .1
	call .1
.1:	push hl
	push de
	push bc
	call LDIRVM
	pop bc
	pop de
	ld a,d
	add a,8
	ld d,a
	pop hl
	ret
    endif

DISSCR:
	call nmi_off
	ld bc,$a201
	call WRTVDP
	jp nmi_on

ENASCR:
	call nmi_off
	ld bc,$e201
	call WRTVDP
	jp nmi_on

CPYBLK:
	pop hl
	ex af,af'
	pop af
	ld b,a
	pop af
	ld c,a
	pop de
	ex (sp),hl
	call nmi_off
.1:	push bc
	push hl
	push de
	ld b,0
	call LDIRVM
	pop hl
    if SMS
	ld bc,$0040
    else
	ld bc,$0020
    endif
	add hl,bc
	ex de,hl
	pop hl
	ex af,af'
	ld c,a
	ld b,0
	add hl,bc
	ex af,af'
	pop bc
	djnz .1
	jp nmi_on
	
nmi_off:
    if COLECO+PV2000
	push hl
	ld hl,mode
	set 0,(hl)
	pop hl
    endif
    if SG1000+SMS+MSX+SVI+SORD+MEMOTECH+NABU
        di
    endif
	ret

nmi_on:
    if COLECO+PV2000
	push af
	push hl
	ld hl,mode
	res 0,(hl)
	nop
	bit 1,(hl)
	jp nz,nmi_handler.0
	pop hl
	pop af
    endif
    if SG1000+SMS+MSX+SVI+SORD+MEMOTECH+NABU
        ei
    endif
	ret

    if COLECO
keypad_table:
        db $0f,$08,$04,$05,$0c,$07,$0b,$02
        db $0d,$0a,$00,$09,$03,$01,$06,$0f
    endif

cls:
    if SMS
	ld hl,$3800
	ld (cursor),hl
	di
	call SETWRT
.1:	ld a,$20	;  7
	out (VDP),a	; 11
	inc hl		;  6
	inc hl		;  6
	ld a,$00	;  7
	out (VDP),a	; 11
	ld a,h		;  4 
	cp $3e		;  7
	jp nz,.1	; 10
	ei
	ret
    else
	ld hl,$1800
	ld (cursor),hl
	ld bc,$0300
	ld a,$20
	call nmi_off
	call FILVRM
	jp nmi_on
    endif

print_string:
	ld c,a
	ld b,0
	ld de,(cursor)
    if SMS
	ld a,d
	and $07
	or $38
	ld d,a
    else
	ld a,d
	and $07
	or $18
	ld d,a
    endif
	push de
	push bc
	call nmi_off
    if SMS
	ex de,hl
.1:	ld a,(de)
	call WRTVRM
	inc de
	inc hl
	xor a
	call WRTVRM
	inc hl
	dec bc
	ld a,b
	or c
	jp nz,.1
    else
	call LDIRVM
    endif
	call nmi_on
	pop bc
	pop hl
	add hl,bc
    if SMS
        add hl,bc
    endif
	ld (cursor),hl
	ret

print_number:
	ld b,0
	call nmi_off
print_number5:
	ld de,10000
	call print_digit
print_number4:
	ld de,1000
	call print_digit
print_number3:
	ld de,100
	call print_digit
print_number2:
	ld de,10
	call print_digit
print_number1:
	ld de,1
	ld b,e
	call print_digit
	jp nmi_on

print_digit:
	ld a,$2f
	or a
.2:	inc a
	sbc hl,de
	jp nc,.2
	add hl,de
	cp $30
	jr nz,.3
	ld a,b
	or a
	ret z
	dec a
	jr z,.4
	ld a,c
	jr print_char
.4:
	ld a,$30
.3:	ld b,1

print_char:
	push hl
	ld hl,(cursor)
	ex af,af'
	ld a,h
	and $07
    if SMS
	or $38
    else
	or $18
    endif
	ld h,a
	ex af,af'
	call WRTVRM
	inc hl
    if SMS
	xor a
	call WRTVRM
	inc hl
    endif
	ld (cursor),hl
	pop hl
	ret

define_char:
	ex de,hl
	ld l,a
	ld h,0
	add hl,hl	; x2
	add hl,hl	; x4
	add hl,hl	; x8
    if SMS
	add hl,hl	; x16
	add hl,hl	; x32
    endif
	ld c,l
	ld b,h
	pop af
	pop hl
	push af
	add hl,hl	; x2
	add hl,hl	; x4
	add hl,hl	; x8
    if SMS
	add hl,hl	; x16
	add hl,hl	; x32
    endif
	ex de,hl
    if SMS
	di
	call LDIRVM
	ei
	ret
    else
	call nmi_off
	ld a,(mode)
	and 8
	jr nz,.1
	call LDIRVM3
	jp nmi_on
	
.1:	call LDIRVM
	jp nmi_on
    endif

define_color:
    if SMS
	ret
    else
	ex de,hl
	ld l,a
	ld h,0
	add hl,hl	; x2
	add hl,hl	; x4
	add hl,hl	; x8
	ld c,l
	ld b,h
	pop af
	pop hl
	push af
	add hl,hl	; x2
	add hl,hl	; x4
	add hl,hl	; x8
	ex de,hl
	set 5,d
	call nmi_off
	call LDIRVM3
	jp nmi_on
    endif

define_sprite:
	ex de,hl
	ld l,a
	ld h,0
	add hl,hl	; x2
	add hl,hl	; x4
	add hl,hl	; x8
	add hl,hl	; x16
	add hl,hl	; x32
    if SMS
	add hl,hl	; x64
    endif
	ld c,l
	ld b,h
	pop af
	pop hl
	push af
	add hl,hl	; x2
	add hl,hl	; x4
    if SMS
	set 1,h
    else
	ld h,$07
    endif
	add hl,hl	; x8
	add hl,hl	; x16
	add hl,hl	; x32
    if SMS
	add hl,hl	; x64
    endif
	ex de,hl
	call nmi_off
	call LDIRVM
	jp nmi_on
    endif
	
update_sprite:
    if SMS
	pop bc		; Pop return address.
	pop de		; 3th. argument in D (Y-coordinate)
	ld e,a		; 4th. argument in E (frame)
	pop af		; 2nd. argument in A (X-coordinate)
	pop hl		; 1st. argument in H (sprite number)
	push bc		; Push return address.
	ld l,h
	res 7,l
	res 6,l
	ld h,sprites>>8
	ld (hl),a
	sla l
	set 7,l
	ld (hl),d
	inc l
	ld (hl),e
    else
	pop bc
	ld (sprite_data+3),a
	pop af
	ld (sprite_data+2),a
	pop af
	ld (sprite_data+1),a
	pop af
	ld (sprite_data),a
	pop af
	; A = Sprite number
	push bc
	ld de,sprites
	add a,a
	add a,a
    if SORD
	or $80
    endif
	ld e,a
	ld hl,sprite_data
	ld bc,4
	ldir
    endif
	ret

	; Fast 16-bit multiplication.
_mul16:
	ld b,h
	ld c,l
	ld a,16
	ld hl,0
.1:
	srl d
	rr e
	jr nc,.2
	add hl,bc
.2:	sla c
	rl b
	dec a
	jp nz,.1
	ret

	; 16-bit signed modulo.
	; hl = hl % de
_mod16s:
	ld a,h
	or a
	push af
	bit 7,h
	call nz,_neg16
	ex de,hl
	bit 7,h
	call nz,_neg16
	ex de,hl
	call _mod16
	pop af
	ret p
	jp _neg16

	; 16-bit signed division.
	; hl = hl / de
_div16s:
	ld a,h
	xor d
	push af
	bit 7,h
	call nz,_neg16
	ex de,hl
	bit 7,h
	call nz,_neg16
	ex de,hl
	call _div16
	pop af
	ret p
	jp _neg16

_abs16:
	bit 7,h
	ret z
_neg16:
	ld a,h
	cpl
	ld h,a
	ld a,l
	cpl
	ld l,a
	inc hl
	ret

	; Fast 16-bit division.
	; hl = hl / de
_div16:
	ld b,h
	ld c,l
	ld hl,0
	ld a,16
.1:
	rl c
	rl b
	adc hl,hl
	sbc hl,de
	jp nc,.2	
	add hl,de
.2:
	ccf
	dec a
	jp nz,.1
	rl c
	rl b
	ld h,b
	ld l,c
	ret

	; Fast 16-bit modulo.
_mod16:
	ld b,h
	ld c,l
	ld hl,0
	ld a,16
.1:
	rl c
	rl b
	adc hl,hl
	sbc hl,de
	jp nc,.2	
	add hl,de
.2:
	ccf
	dec a
	jp nz,.1
	ret

_sgn16:
	ld a,h
	or l
	ret z
	bit 7,h
	ld hl,$ffff
	ret nz
	inc hl
	inc hl
	ret

	; Random number generator.
	; From my game Mecha Eight.
random:
        ld hl,(lfsr)
        ld a,h
        or l
        jr nz,.0
        ld hl,$7811
.0:     ld a,h
        and $80
        ld b,a
        ld a,h
        and $02
        rrca
        rrca
        xor b
        ld b,a
        ld a,h
        and $01
        rrca
        xor b
        ld b,a
        ld a,l
        and $20
        rlca
        rlca
        xor b
        rlca
        rr h
        rr l
        ld (lfsr),hl
        ret

sn76489_freq:
    if COLECO+SG1000+SMS+SORD+MEMOTECH+PV2000
	ld b,a
	ld a,l
	and $0f
	or b
	out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
	add hl,hl
	add hl,hl
	add hl,hl
	add hl,hl
	ld a,h
	and $3f
	out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
    endif
	ret

sn76489_vol:
    if COLECO+SG1000+SMS+SORD+MEMOTECH+PV2000
	cpl
	and $0f
	or b
	out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
    endif
	ret

sn76489_control:
    if COLECO+SG1000+SMS+SORD+MEMOTECH+PV2000
	and $0f
	or $e0
	out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
    endif
	ret

ay3_reg:
    if COLECO
	push af
	ld a,b
	out ($50),a
	pop af
	out ($51),a
	ret
    endif
    if SG1000+SORD+SMS+MEMOTECH+PV2000
        ret
    endif
    if MSX+SVI+EINSTEIN+NABU
	ld e,a
	ld a,b
	jp WRTPSG
    endif

ay3_freq:
    if COLECO
	out ($50),a
	push af
	ld a,l
	out ($51),a
	pop af
	inc a
	out ($50),a
	push af
	ld a,h
	and $0f
	out ($51),a
	pop af
	ret
    endif
    if SG1000+SMS+SORD+MEMOTECH+PV2000
	ret
    endif
    if MSX+SVI+EINSTEIN+NABU
	ld e,l
	call WRTPSG
	ld e,h
	inc a
	jp WRTPSG
    endif

    if SG1000+SMS+SVI+SORD+MEMOTECH+EINSTEIN+PV2000+NABU
	; Required for SG1000 and Sega Master System as both don't have a BIOS
	; Required for SVI because we don't have access to BIOS in cartridge.
	; Required for Sord M5 because it doesn't provide an ASCII charset.
	; Required for Memotech/Einstein because CP/M uses the memory.
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
    endif

    if SMS
palette_load:
	push hl
	di
	ld hl,$c000
	call SETWRT
	pop hl
	ld bc,32*256+VDP
	outi
	jp nz,$-2
	ei
	ret

mode_4:
	di
	ld bc,$0400
	call WRTVDP
	ld bc,$a201
	call WRTVDP
	ld bc,$0f02	; $3800 for pattern table (required bit 0 set to 1 for SMS1)
	call WRTVDP
	ld bc,$ff03	; Not used (required value for SMS1)
	call WRTVDP
	ld bc,$0704	; Not used (required value for SMS1)
	call WRTVDP
	ld bc,$7f05	; $3f00 for sprite attribute table (required bit 0 set to 1)
	call WRTVDP
	ld bc,$0706	; $2000 for sprites bitmaps (or $03 for $0000)
	call WRTVDP
	ld bc,$0007	
	call WRTVDP
	ld bc,$0008	; Background X scroll	
	call WRTVDP
	ld bc,$0009	; Background Y scroll	
	call WRTVDP
	ld hl,$20*32	; Point to space character bitmap
	call SETWRT
	ld hl,font_bitmaps
	ld c,96
.1:	ld b,8
.2:	ld a,(hl)
	out (VDP),a
	ld a,(hl)
	out (VDP),a
	ld a,(hl)
	out (VDP),a
	ld a,(hl)
	out (VDP),a
	inc hl
	djnz .2
	dec c
	jp nz,.1
	ei
	call cls
	di
	ld hl,$3f00
	ld bc,$0040
	ld a,$e0
	call FILVRM
	ld hl,sprites
	ld de,sprites+1
	ld bc,63
	ld (hl),$e0
	ldir
	ei
	ld hl,.3
	call palette_load
	jp ENASCR

	; TMS9118-alike palette
.3:
	db $00,$00,$0c,$2e,$20,$30,$02,$3c,$17,$2B,$0f,$2f,$08,$33,$2a,$3f
	db $00,$00,$0c,$2e,$20,$30,$02,$3c,$17,$2B,$0f,$2f,$08,$33,$2a,$3f

    else
vdp_generic_mode:
	call nmi_off
	call WRTVDP
	ld bc,$a201
	call WRTVDP
	ld bc,$0602	; $1800 for pattern table.
	call WRTVDP
	ld b,d
	ld c,$03	; for color table.
	call WRTVDP
	ld b,e
	ld c,$04	; for bitmap table.
	call WRTVDP
	ld bc,$3605	; $1b00 for sprite attribute table.
	call WRTVDP
	ld bc,$0706	; $3800 for sprites bitmaps.
	call WRTVDP
	ld bc,$0107
	jp WRTVDP

mode_0:
	ld hl,mode
	res 3,(hl)
	ld bc,$0200
	ld de,$ff03	; $2000 for color table, $0000 for bitmaps.
	call vdp_generic_mode
    if COLECO
      if PENCIL
        ld hl,($0013)
      else
	ld hl,($006c)
      endif
	ld de,-128
	add hl,de
    endif
    if INCLUDE_FONT_DATA
	ld hl,font_bitmaps
    endif
    if MSX
	ld hl,($0004)   
	inc h
    endif
	ld de,$0100
	ld bc,$0300
	call LDIRVM3
	call nmi_on
	call nmi_off
	ld hl,$2000
	ld bc,$1800
	ld a,$f0
	call FILVRM
	call nmi_on
	call cls
vdp_generic_sprites:
	call nmi_off
	ld hl,$1b00
	ld bc,$0080
	ld a,$d1
	call FILVRM
	ld hl,sprites
	ld de,sprites+1
	ld bc,127
	ld (hl),$d1
	ldir
	call nmi_on
	jp ENASCR

mode_1:
	ld hl,mode
	res 3,(hl)
	ld bc,$0200
	ld de,$ff03	; $2000 for color table, $0000 for bitmaps.
	call vdp_generic_mode
	ld hl,$0000
	ld bc,$1800
	xor a
	call FILVRM
	call nmi_on
	call nmi_off
	ld hl,$2000
	ld bc,$1800
	ld a,$f0
	call FILVRM
	call nmi_on
	ld hl,$1800
.1:	call nmi_off
	ld b,32
.2:	ld a,l
	call WRTVRM
	inc hl
	djnz .2
	call nmi_on
	ld a,h
	cp $1b
	jp nz,.1
	jp vdp_generic_sprites

mode_2:
	ld hl,mode
	set 3,(hl)
	ld bc,$0000
	ld de,$8000	; $2000 for color table, $0000 for bitmaps.
	call vdp_generic_mode
    if COLECO
      if PENCIL
        ld hl,($0013)
      else
	ld hl,($006c)
      endif
	ld de,-128
	add hl,de
    endif
    if INCLUDE_FONT_DATA
	ld hl,font_bitmaps
    endif
    if MSX
	ld hl,($0004)   
	inc h
    endif
	ld de,$0100
	ld bc,$0300
	call LDIRVM
	call nmi_on
	call nmi_off
	ld hl,$2000
	ld bc,$0020
	ld a,$f0
	call FILVRM
	call nmi_on
	call cls
	jp vdp_generic_sprites
    endif

    if MSX
ENASLT: EQU $0024       ; Select slot (H=Addr, A=Slot)
RSLREG: EQU $0138       ; Read slot status in A

        ;
        ; Get slot mapping
        ; B = 16K bank (0 for $0000, 1 for $4000, 2 for $8000, 3 for $c000)
        ; A = Current slot selection status (CALL RSLREG)
        ;
get_slot_mapping:
        call rotate_slot
        ld c,a
        add a,$C1       ; EXPTBL
        ld l,a
        ld h,$FC
        ld a,(hl)
        and $80         ; Get expanded flag
        or c
        ld c,a
        inc hl
        inc hl
        inc hl
        inc hl
        ld a,(hl)       ; SLTTBL
        call rotate_slot
        rlca
        rlca
        or c            ; A contains bit 7 = Marks expanded
                        ;            bit 6 - 4 = Doesn't care
                        ;            bit 3 - 2 = Secondary mapper
                        ;            bit 1 - 0 = Primary mapper
        ret

rotate_slot:
        push bc
        dec b
        inc b
        jr z,.1
.0:     rrca
        rrca
        djnz .0
.1:     and 3
        pop bc
        ret

    endif

nmi_handler:
	push af
	push hl
	ld hl,mode
	bit 0,(hl)
	jr z,.1
	set 1,(hl)
	pop hl
	pop af
	retn

.0:	res 1,(hl)

.1:	push bc
	push de
  if CVBASIC_BANK_SWITCHING
    if COLECO
	ld a,($ffbf)
    endif
    if SG1000+SMS
	ld a,($7fbf)
    endif
    if MSX
	ld a,($bfff)
    endif
	push af
  endif
    if SORD+MEMOTECH
        call ctc_reti
    endif
    if SG1000+SMS+MSX+SVI+SORD+MEMOTECH+NABU
	in a,(VDPR+1)
	ld (vdp_status),a
    endif

	;
	; Update of sprite attribute table
	;
    if SMS
	bit 2,(hl)
	jr z,.4
	ld hl,$3f00
	call SETWRT
	ld hl,sprites
	ld bc,$4000+VDP
	outi
	jp nz,$-2
	ld hl,$3f80
	call SETWRT
	ld hl,sprites
	ld bc,$8000+VDP
	outi
	jp nz,$-2
	jr .5

.4:	ld hl,$3f00
	call SETWRT
	ld a,(flicker)
	inc a
	and $3f
	ld (flicker),a
	ld l,a
	ld h,sprites>>8
	ld bc,$4000+VDP
	ld de,6
	outi
	add hl,de
	res 6,l
	jp nz,$-5
	ld hl,$3f80
	call SETWRT
	ld a,(flicker)
	add a,a
	or $80
	ld l,a
	ld bc,$8000+VDP
	ld de,12
	ld h,sprites>>8
	outi
	nop
	nop
	nop
	outi
	add hl,de
	set 7,l
	jp nz,$-12
.5:
    else
	ld bc,$8000+VDP
	bit 2,(hl)
	jr z,.4

	ld hl,$1b00
	call SETWRT
	ld hl,sprites
.7:
    if PV2000
	ld a,(hl)
	ld (VDP),a
	inc hl
	djnz .7
    else
    if MEMOTECH+EINSTEIN+SG1000
	nop
	nop
    endif
	outi
	jp nz,.7
    endif
	jr .5

.4:
	ld hl,$1b00
	call SETWRT
	ld a,(flicker)
	add a,$04
	ld (flicker),a
	ld de,24
	ld b,128
	ld l,a
    if SORD
    else
	ld h,sprites>>8
    endif
.6:
    if SORD
	set 7,l
	ld h,sprites>>8
    else
	res 7,l
    endif
    if PV2000
	ld a,(hl)	;  7  8
	ld (VDP),a	; 13 14
	inc hl		;  6  7
	dec b		;  4  5
	ld a,(hl)
	ld (VDP),a
	inc hl
	dec b
	ld a,(hl)
	ld (VDP),a
	inc hl
	dec b
	ld a,(hl)
	ld (VDP),a
	inc hl
	dec b
    else
	outi
	jp $+3
    if MEMOTECH+EINSTEIN+SG1000
	nop
	nop
    endif
	outi
	jp $+3
    if MEMOTECH+EINSTEIN+SG1000
	nop
	nop
    endif
	outi
	jp $+3
    if MEMOTECH+EINSTEIN+SG1000
	nop
	nop
    endif
	outi
	jp $+3
    if MEMOTECH+EINSTEIN+SG1000
	nop
	nop
    endif
    endif
	add hl,de
	jp nz,.6
.5:
    endif

    if COLECO
	out (JOYSEL),a
	ex (sp),hl
	ex (sp),hl
	in a,(JOY1)
	or $b0
	ld b,a
	in a,(JOY2)
	or $b0
	ld c,a

	out (KEYSEL),a
	ex (sp),hl
	ex (sp),hl
	in a,(JOY1)
	ld d,a
	in a,(JOY2)
	ld e,a

	ld a,d
	rlca
	or $7f
	and b
	cpl
	ld (joy1_data),a

	ld a,e
	rlca
	or $7f
	and c
	cpl
	ld (joy2_data),a

	ld a,d
	and $0f
	ld c,a
	ld b,0
	ld hl,keypad_table
	add hl,bc
	ld a,(hl)
	ld (key1_data),a

	ld a,e
	and $0f
	ld c,a
	ld hl,keypad_table
	add hl,bc
	ld a,(hl)
	ld (key2_data),a
    endif
    if SG1000+SMS
	ld a,$07
	out ($de),a
	ld b,$ff
	in a,(JOY1)
	ld h,a
	in a,(JOY2)
	ld l,a
	in a,($de)
	cp 7
	jp nz,.sg1000

	ld a,$00
	out ($de),a
	in a,($dc)
	rra
	ld c,1
	jr nc,.sg1
	in a,($dd)
	rra
	ld c,8
	jr nc,.sg1
	ld a,$01
	out ($de),a
	in a,($dc)
	rra
	ld c,2
	jr nc,.sg1
	in a,($dd)
	rra
	ld c,9
	jr nc,.sg1
	ld a,$02
	out ($de),a
	in a,($dc)
	rra
	ld c,3
	jr nc,.sg1
	in a,($dd)
	rra
	ld c,0
	jr nc,.sg1
	ld a,$03
	out ($de),a
	in a,($dc)
	bit 4,a
	ld c,10
	jr z,.sg1
	rra
	ld c,4
	jr nc,.sg1
	ld a,$04
	out ($de),a
	in a,($dc)
	rra
	ld c,5
	jr nc,.sg1
	ld a,$05
	out ($de),a
	in a,($dc)
	bit 6,a
	ld c,11
	jr z,.sg1
	rra
	ld c,6
	jr nc,.sg1
	ld a,$06
	out ($de),a
	in a,($dc)
	rra
	ld c,7
	jr nc,.sg1
	ld c,15
.sg1:	ld a,c
	ld (key1_data),a

	ld a,$04
	out ($de),a
	in a,($dc)
	bit 5,a		; Keyboard down.
	jr nz,$+4
	res 1,h
	ld a,$05
	out ($de),a
	in a,($dc)
	bit 5,a		; Keyboard left.
	jr nz,$+4
	res 2,h
	ld a,$06
	out ($de),a
	in a,($dc)
	bit 5,a		; Keyboard right.
	jr nz,$+4
	res 3,h
	bit 6,a		; Keyboard up.
	jr nz,$+4
	res 0,h
	ld a,$02
	out ($de),a
	in a,($dc)
	bit 4,a		; Keyboard Ins
	jr nz,$+4
	res 4,h
	ld a,$03
	out ($de),a
	in a,($dc)
	bit 4,a		; Keyboard Del
	jr nz,$+4
	res 5,h
.sg1000:
        bit 0,h
        jr nz,$+4
        res 0,b
        bit 1,h
        jr nz,$+4
        res 2,b
        bit 2,h
        jr nz,$+4
        res 3,b
        bit 3,h
        jr nz,$+4
        res 1,b
        bit 4,h
        jr nz,$+4
        res 6,b
        bit 5,h
        jr nz,$+4
        res 7,b
	ld a,b
	cpl
	ld (joy1_data),a

	ld a,$ff
        bit 6,h
        jr nz,$+4
        res 0,a
        bit 7,h
        jr nz,$+4
        res 2,a

        bit 0,l
        jr nz,$+4
        res 3,a
        bit 1,l
        jr nz,$+4
        res 1,a
        bit 2,l
        jr nz,$+4
        res 4,a
        bit 3,l
        jr nz,$+4
        res 5,a
	cpl
	ld (joy2_data),a

    endif
    if MSX
	; Keyboard matrix from https://map.grauw.nl/articles/keymatrix.php
	ld a,15
	call RDPSG
	and $b0
	or $4f
	ld e,a
	ld a,15
	call WRTPSG
	ld a,14
	call RDPSG
	ld b,$ff
	bit 0,a
	jr nz,$+4
	res 0,b
	bit 3,a
	jr nz,$+4
	res 1,b
	bit 1,a
	jr nz,$+4
	res 2,b
	bit 2,a
	jr nz,$+4
	res 3,b
	bit 4,a
	jr nz,$+4
	res 6,b
	bit 5,a
	jr nz,$+4
	res 7,b
	ld a,b
	cpl
	ld (joy2_data),a

	in a,($aa)
	and $f0
	or $00
	out ($aa),a
	in a,($a9)
	cp $ff
	ld c,$ff
	jr nz,.key1
	in a,($aa)
	and $f0
	or $01
	out ($aa),a
	in a,($a9)
	and $03
	cp $03
	ld c,$07
	jr nz,.key1
	in a,($aa)
	and $f0
	or $07
	out ($aa),a
	in a,($a9)
	bit 5,a		; BS
	ld c,$0a
	jr z,.key2
	bit 7,a		; RET
	ld c,$0b
	jr z,.key2
	ld c,$0f
	jr .key2

.key1:	rra
	inc c
	jr c,.key1
.key2:
	ld a,c
	ld (key1_data),a	

        ld b,$ff
	in a,($aa)
	and $f0
	or $08
	out ($aa),a
	in a,($a9)
	bit 5,a
	jr nz,$+4
        res 0,b
	bit 7,a
	jr nz,$+4
        res 1,b
        bit 6,a
        jr nz,$+4
        res 2,b
        bit 4,a
        jr nz,$+4
        res 3,b
	bit 0,a
	jr nz,$+4
	res 6,b
	in a,($aa)
	and $f0
	or $04
	out ($aa),a
	in a,($a9)
	bit 2,a
	jr nz,$+4
	res 7,b

	ld a,15
	call RDPSG
	and $b0
	or $0f
	ld e,a
	ld a,15
	call WRTPSG
	ld a,14
	call RDPSG
	bit 0,a
	jr nz,$+4
	res 0,b
	bit 3,a
	jr nz,$+4
	res 1,b
	bit 1,a
	jr nz,$+4
	res 2,b
	bit 2,a
	jr nz,$+4
	res 3,b
	bit 4,a
	jr nz,$+4
	res 6,b
	bit 5,a
	jr nz,$+4
	res 7,b

	ld a,b
	cpl
	ld (joy1_data),a
    endif
    if SVI
	ld a,14
	call RDPSG
	ld b,$ff
	bit 4,a
	jr nz,$+4
	res 0,b
	bit 7,a
	jr nz,$+4
	res 1,b
	bit 5,a
	jr nz,$+4
	res 2,b
	bit 6,a
	jr nz,$+4
	res 3,b

	in a,($98)
	bit 5,a
	jr nz,$+4
	res 6,b
	ld a,b
	cpl
	ld (joy2_data),a

	ld a,$10
	out ($96),a
	in a,($99)
	cp $ff
	ld c,$ff
	jr nz,.key1
	ld a,$11
	out ($96),a
	in a,($99)
	and $03
	cp $03
	ld c,$07
	jr nz,.key1
	ld a,$16
	out ($96),a
	in a,($99)
	bit 6,a
	ld c,$0b
	jr z,.key2
	ld a,$15
	out ($96),a
	in a,($99)
	bit 6,a
	ld c,$0a
	jr z,.key2
	ld c,$0f
	jr .key2

.key1:	rra
	inc c
	jr c,.key1
.key2:
	ld a,c
	ld (key1_data),a	

        ld b,$ff
	ld a,$15
	out ($96),a
	in a,($99)
	bit 7,a
	jr nz,$+4
        res 0,b
	ld a,$18
	out ($96),a
	in a,($99)
	bit 7,a
	jr nz,$+4
        res 1,b
	ld a,$17
	out ($96),a
	in a,($99)
	bit 7,a
        jr nz,$+4
        res 2,b
	ld a,$16
	out ($96),a
	in a,($99)
	bit 7,a
        jr nz,$+4
        res 3,b
	ld a,$18
	out ($96),a
	in a,($99)
	bit 0,a
	jr nz,$+4
	res 6,b
	ld a,$13
	out ($96),a
	in a,($99)
	bit 5,a
	jr nz,$+4
	res 7,b

	ld a,14
	call RDPSG
	bit 0,a
	jr nz,$+4
	res 0,b
	bit 3,a
	jr nz,$+4
	res 1,b
	bit 1,a
	jr nz,$+4
	res 2,b
	bit 2,a
	jr nz,$+4
	res 3,b
	
	in a,($98)
	bit 4,a
	jr nz,$+4
	res 6,b
	ld a,b
	cpl
	ld (joy1_data),a
    endif
    if SORD
	ld bc,$ffff
	in a,($37)	; Read joystick
	rra
	jr nc,$+4
	res 1,b
	rra
	jr nc,$+4
	res 0,b
	rra
	jr nc,$+4
	res 3,b
	rra
	jr nc,$+4
	res 2,b
	rra
	jr nc,$+4
	res 1,c
	rra
	jr nc,$+4
	res 0,c
	rra
	jr nc,$+4
	res 3,c
	rra
	jr nc,$+4
	res 2,c
	in a,($31)
	rra
	jr nc,$+4
	res 6,b
	rra
	jr nc,$+4
	res 7,b
	rra
	rra
	rra
	jr nc,$+4
	res 6,c
	rra
	jr nc,$+4
	res 7,c
	ld a,b
	cpl
	ld (joy1_data),a
	ld a,c
	cpl
	ld (joy2_data),a
	in a,($31)	; Keyboard 1-8
	or a
	ld c,$00
	jr nz,.key3
	in a,($35)	
	bit 0,a		; 9
	ld c,$09
	jr nz,.key5
	bit 1,a		; 0
	ld c,$00
	jr nz,.key5
	bit 7,a		; Backspace
	ld c,$0a
	jr nz,.key5
	in a,($30)
	bit 7,a		; Enter
	ld c,$0b	
	jr nz,.key5
	ld c,$0f
	jr .key5

.key3:	rra
	inc c
	jr nc,.key3
.key5:
	ld a,c
	ld (key1_data),a	

    endif
    if MEMOTECH
	ld bc,$ffff

        ld a,$fb
        out ($05),a	; Select keyboard row.
        ex (sp),hl
        ex (sp),hl
        in a,($05)	; Read keyboard data.
        bit 7,a
        jr nz,$+4
        res 0,b         ; Up direction.
        bit 3,a		; Y
        jr nz,$+4
        res 0,c		; Up direction.

        ld a,$ef
        out ($05),a	; Select keyboard row.
        ex (sp),hl
        ex (sp),hl
        in a,($05)	; Read keyboard data.
        bit 7,a
        jr nz,$+4
        res 1,b         ; Right key.
        ld a,$bf
        out ($05),a	; Select keyboard row.
        ex (sp),hl
        ex (sp),hl
        in a,($05)	; Read keyboard data.
        bit 7,a
        jr nz,$+4
        res 2,b         ; Down key.
        ld a,$f7
        out ($05),a	; Select keyboard row.
        ex (sp),hl
        ex (sp),hl
        in a,($05)	; Read keyboard data.
        bit 7,a
        jr nz,$+4
        res 3,b         ; Left key.
        ld a,$df
        out ($05),a	; Select keyboard row.
        ex (sp),hl
        ex (sp),hl
        in a,($05)	; Read keyboard data.
        bit 7,a         ; Home key.
        jr nz,$+4
        res 6,b         ; Button 1

        ld a,$7F
        out ($05),a	; Select keyboard row.
        ex (sp),hl
        ex (sp),hl
        in a,($05)	; Read keyboard data.
        bit 2,a		; B
        jr nz,$+4
        res 0,c         ; Up key.
        bit 1,a		; C
        jr nz,$+4
        res 1,c         ; Right key.
        bit 3,a		; M
        jr nz,$+4
        res 2,c         ; Down key.
        bit 0,a		; Z
        jr nz,$+4
        res 3,c         ; Left key.
	bit 5,a		; -
	jr nz,$+4
	res 7,c		; Button 2 for player 2.
	bit 7,a		; -
	jr nz,$+4
	res 7,b		; Button 2 for player 1.
        in a,($06)	; Read keyboard data.
        bit 0,a         ; Space
        jr nz,$+4
        res 6,c         ; Button 1.
	ld a,b
	cpl
	ld (joy1_data),a
	ld a,c
	cpl
	ld (joy2_data),a

	ld a,$fe
	out ($05),a
	ex (sp),hl
	ex (sp),hl
	in a,($05)
	rra
	ld b,1
	jr nc,.mt1
	rra
	ld b,3
	jr nc,.mt1
	rra
	ld b,5
	jr nc,.mt1
	rra
	ld b,7
	jr nc,.mt1
	rra
	ld b,9
	jr nc,.mt1
	ld a,$df
	out ($05),a
	ex (sp),hl
	ex (sp),hl
	in a,($05)
	bit 6,a
	ld b,11
	jr z,.mt1
	ld a,$fd
	out ($05),a
	ex (sp),hl
	ex (sp),hl
	in a,($05)
	rra
	rra
	ld b,2
	jr nc,.mt1
	rra
	ld b,4
	jr nc,.mt1
	rra
	ld b,6
	jr nc,.mt1
	rra
	ld b,8
	jr nc,.mt1
	rra
	ld b,0
	jr nc,.mt1
	in a,($06)
	rra
	ld b,10
	jr nc,.mt1
	ld b,15
.mt1:
	ld a,b
	ld (key1_data),a
    endif
    if EINSTEIN
	ld bc,$ffff
        ld a,$0e
        out ($02),a  
        ld a,$f7
        out ($03),a
        ex (sp),hl
        ex (sp),hl
        ld a,$0f
        out ($02),a  
        in a,($02)
        bit 6,a
        jr nz,$+4
        res 0,b		; Up
        ld a,$0e
        out ($02),a
        ld a,$fb
        out ($03),a
        ex (sp),hl
        ex (sp),hl
        ld a,$0f
        out ($02),a 
        in a,($02)
        bit 4,a
        jr nz,$+4
        res 1,b		; Right
        ld a,$0e
        out ($02),a
        ld a,$fd
        out ($03),a
        ex (sp),hl
        ex (sp),hl
        ld a,$0f
        out ($02),a
        in a,($02)
        bit 5,a
        jr nz,$+4
        res 2,b		; Down
        ld a,$0e
        out ($02),a 
        ld a,$fd
        out ($03),a
        ex (sp),hl
        ex (sp),hl
        ld a,$0f
        out ($02),a
        in a,($02)
        bit 3,a
        jr nz,$+4
        res 3,b		; Left
        ld a,$0e
        out ($02),a
        ld a,$fe
        out ($03),a
        ex (sp),hl
        ex (sp),hl
        ld a,$0f
        out ($02),a  
        in a,($02)
        bit 6,a
        jr nz,$+4
        res 6,b         ; Fire
        ld a,$0e
        out ($02),a
        ld a,$7f
        out ($03),a
        ex (sp),hl
        ex (sp),hl
        ld a,$0f
        out ($02),a  
        in a,($02)
        bit 0,a
        jr nz,$+4
        res 7,b         ; 2nd fire
	ld a,b
	cpl
	ld (joy1_data),a
	ld a,c
	cpl
	ld (joy2_data),a

        ld a,$0e
        out ($02),a
        ld a,$ef
        out ($03),a
        ex (sp),hl
        ex (sp),hl
        ld a,$0f
        out ($02),a  
        in a,($02)
	rra
	ld b,7
	jr nc,.te1
	rra
	ld b,6
	jr nc,.te1
	rra
	ld b,5
	jr nc,.te1
	rra
	ld b,4
	jr nc,.te1
	rra
	ld b,3
	jr nc,.te1
	rra
	ld b,2
	jr nc,.te1
	rra
	ld b,1
	jr nc,.te1
        ld a,$0e
        out ($02),a
        ld a,$f7
        out ($03),a
        ex (sp),hl
        ex (sp),hl
        ld a,$0f
        out ($02),a  
        in a,($02)
	bit 3,a
	ld b,8
	jr z,.te1
	bit 4,a
	ld b,10
	jr z,.te1
        ld a,$0e
        out ($02),a
        ld a,$fb
        out ($03),a
        ex (sp),hl
        ex (sp),hl
        ld a,$0f
        out ($02),a  
        in a,($02)
	bit 6,a
	ld b,9
	jr z,.te1
        ld a,$0e
        out ($02),a
        ld a,$fe
        out ($03),a
        ex (sp),hl
        ex (sp),hl
        ld a,$0f
        out ($02),a  
        in a,($02)
	bit 5,a
	ld b,11
	jr z,.te1
        ld a,$0e
        out ($02),a
        ld a,$fd
        out ($03),a
        ex (sp),hl
        ex (sp),hl
        ld a,$0f
        out ($02),a  
        in a,($02)
	bit 7,a
	ld b,0
	jr z,.te1
	ld b,15
.te1:
	ld a,b
	ld (key1_data),a
    endif
    if PV2000
	ld bc,$ffff
	ld a,7
	out ($20),a
	ex (sp),hl
	ex (sp),hl
	in a,($20)
	bit 1,a
	jr z,$+4
	res 0,b
	bit 0,a
	jr z,$+4
	res 3,b
	ld a,6
	out ($20),a
	ex (sp),hl
	ex (sp),hl
	in a,($20)
	bit 1,a
	jr z,$+4
	res 1,b
	bit 0,a
	jr z,$+4
	res 2,b
	ld a,8
	out ($20),a
	ex (sp),hl
	ex (sp),hl
	in a,($20)
	bit 0,a
	jr z,$+4
	res 6,b
	bit 1,a
	jr z,$+4
	res 7,b
	ld a,b
	cpl
	ld (joy1_data),a
	ld a,c
	cpl
	ld (joy2_data),a
	ld a,0
	out ($20),a
	ex (sp),hl
	ex (sp),hl
	in a,($20)
	and $0f
	ld b,$04
	jr nz,.pv1
	in a,($10)
	and $0f
	ld b,$08
	jr nz,.pv1
	ld a,8
	out ($20),a
	ex (sp),hl
	ex (sp),hl
	in a,($10)
	ld b,11
	bit 0,a
	jr nz,.pv2
	ld a,4
	out ($20),a
	ex (sp),hl
	ex (sp),hl
	in a,($20)
	ld b,10
	bit 3,a
	jr nz,.pv2
	in a,($10)
	ld b,9
	bit 0,a
	jr nz,.pv2
	ld b,0
	bit 3,a
	jr nz,.pv2
	ld b,15
	jr .pv2

.pv1:	rra
	jr c,.pv2
	dec b
	rra
	jr c,.pv2
	dec b
	rra
	jr c,.pv2
	dec b
.pv2:	ld a,b
	ld (key1_data),a
    endif
    if NABU
	ld a,(nabu_data0)
	and $cf
	ld (joy1_data),a
	ld a,(nabu_data1)
	and $cf
	ld (joy2_data),a
	ld a,(nabu_data2)
	ld (key1_data),a
	ld a,$0f
	ld (nabu_data2),a
    endif

    if CVBASIC_MUSIC_PLAYER
	ld a,(music_mode)
	or a
	call nz,music_hardware
    endif

	ld hl,(frame)
	inc hl
	ld (frame),hl

	ld hl,lfsr	; Make LFSR more random
	inc (hl)
	inc (hl)
	inc (hl)

    if CVBASIC_MUSIC_PLAYER
	;
	; Music is played with a 50hz clock.
	;
	ld a,(ntsc)
	or a
	jr z,.2
	ld a,(music_tick)
	inc a
	cp 6
	jr nz,$+3
	xor a
	ld (music_tick),a
	jr z,.3
.2:
	ld a,(music_mode)
	or a
	call nz,music_generate
.3:
    endif
	;CVBASIC MARK DON'T CHANGE

  if CVBASIC_BANK_SWITCHING
	pop af
    if COLECO
	ld l,a
	ld h,$ff
	ld a,(hl)
    endif
    if SG1000+SMS
	ld ($fffe),a
    endif
    if MSX
	ld ($7000),a
    endif
  endif
	pop de
	pop bc
	pop hl
    if COLECO
	in a,(VDP+1)
	ld (vdp_status),a
	pop af
	retn
    endif
    if SG1000+SMS+SVI+NABU
	pop af
        ei
        reti
    endif
    if MSX
	pop af
        ret
    endif
    if SORD+MEMOTECH
	pop af
        ei
        ret

ctc_reti:
	reti
    endif
    if EINSTEIN
	pop af
	ret
    endif
    if PV2000
	ld a,(VDP+1)
	ld (vdp_status),a
	pop af
	retn
    endif

    if NABU
keyboard_handler:
	push af
	push bc
	push hl
	in a,($90)
	ld c,a
	cp $0d	; Enter
	ld a,11
	jr z,.8
	ld a,c
	cp $7f	; Del
	ld a,10
	jr z,.8
	ld a,c
	cp $30
	jr c,.7
	cp $3a
	jr nc,.7
	sub $30
.8:	ld (nabu_data2),a
	jr .1
.7:
	cp $a0
	jr c,.2
	cp $c0
	jr c,.joystick
.2:
	ld hl,nabu_data0
	cp $80
	jr nz,.3
	res 4,(hl)
	jr .1
.3:
	cp $81
	jr nz,.4
	set 4,(hl)
	jr .1
.4:
	cp $e0
	jr c,.1
	and $0f
	ld b,2
	jr z,.5
	dec a
	ld b,8
	jr z,.5
	dec a
	ld b,1
	jr z,.5
	dec a
	ld b,4
	jr z,.5
	dec a
	ld b,$80
	jr z,.5
	dec a
	ld b,$40
	jr nz,.1
.5:	bit 4,c
	jr nz,.6
	ld a,(hl)
	or b
	ld (hl),a
	jr .1

.6:
	ld a,b
	cpl
	and (hl)
	ld (hl),a
	jr .1

.joystick:
	ld hl,nabu_data0
	bit 4,(hl)
	jr z,$+5
	ld hl,nabu_data1
	ld b,$00
	bit 3,c
	jr z,$+4
	set 0,b
	bit 2,c
	jr z,$+4
	set 1,b
	bit 1,c
	jr z,$+4
	set 2,b
	bit 0,c
	jr z,$+4
	set 3,b
	bit 4,c
	jr z,$+4
	set 6,b
	ld (hl),b
.1:
	pop hl
	pop bc
	pop af
	ei
	reti
    endif
    if SORD
wait:
	ld de,(frame)
.1:
	ld hl,(frame)
	or a
	sbc hl,de
	jr z,.1
	ret
    endif
    if EINSTEIN
wait:
	in a,(VDP+1)
	nop
	in a,(VDP+1)
	bit 7,a
	jr z,$-4
	ld (vdp_status),a
	jp nmi_handler
    endif

	;
	; The music player code comes from my
	; game Princess Quest for Colecovision (2012)
	;

        ;
        ; Init music player.
        ;
music_init:
    if COLECO+SG1000+SMS+SORD+MEMOTECH+PV2000
        ld a,$9f
        out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
        ld a,$bf
        out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
        ld a,$df
        out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
        ld a,$ff
        out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
        ld a,$ec
        out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
    endif
    if COLECO+SG1000+SMS+MSX+SVI+SORD+MEMOTECH+PV2000
MIX_BASE:	equ $b8
    endif
    if EINSTEIN+NABU
MIX_BASE:	equ $78
    endif
    if MSX+SVI+EINSTEIN+NABU
	ld a,$08
	ld e,$00
	call WRTPSG
	ld a,$09
	ld e,$00
	call WRTPSG
	ld a,$0a
	ld e,$00
	call WRTPSG
	ld a,$07
	ld e,MIX_BASE
	call WRTPSG
    endif
    if SGM
	ld b,$08
	xor a
	call ay3_reg
	ld b,$09
	call ay3_reg
	ld b,$0a
	call ay3_reg
	ld b,$07
	ld a,$b8
	call ay3_reg
    endif
    if CVBASIC_MUSIC_PLAYER
    else
	ret
    endif

    if CVBASIC_MUSIC_PLAYER
        ld a,$ff
        ld (audio_vol4hw),a
        ld a,$ec
        ld (audio_control),a
        ld a,MIX_BASE
        ld (audio_mix),a
	ld hl,music_silence
        ;
	; Play a music.
	; HL = Pointer to music.
        ;
music_play:
        call nmi_off
        ld a,(hl)          
        ld (music_timing),a
        inc hl
        ld (music_start),hl
        ld (music_pointer),hl
        xor a
        ld (music_note_counter),a
	inc a
	ld (music_playing),a
  if CVBASIC_BANK_SWITCHING
    if COLECO
	ld a,($ffbf)
    endif
    if SG1000+SMS
        ld a,($7fbf)
    endif
    if MSX
        ld a,($bfff)
    endif
        ld (music_bank),a
  endif
        jp nmi_on

        ;
        ; Generates music.
        ;
music_generate:
        ld a,(audio_mix)
        and $c0                 
        or $38
        ld (audio_mix),a
        xor a                ; Turn off all the sound channels.
        ld l,a
        ld h,a
        ld (audio_vol1),hl   ; audio_vol1/audio_vol2
        ld (audio_vol3),a
	ld a,$ff
	ld (audio_vol4hw),a

        ld a,(music_note_counter)
        or a
        jp nz,.6
        ld hl,(music_pointer)
.15:    push hl
  if CVBASIC_BANK_SWITCHING
	ld a,(music_bank)
    if COLECO
	ld e,a
	ld d,$ff
	ld a,(de)
    endif
    if SG1000+SMS
        ld ($fffe),a
    endif
    if MSX
	ld ($7000),a
    endif
  endif
        ld b,(hl)
        inc hl
        ld c,(hl)
        inc hl
        ld d,(hl)
        inc hl
        ld e,(hl)
        pop hl
        ld a,(music_timing)
        rlca
        jr nc,.16
        ld e,d
        ld d,0
        jr .17

.16:    rlca
        jr nc,.17
        ld e,0
.17:    ld a,b		; Read first byte.
        cp -2           ; End of music?
        jr nz,.19       ; No, jump.
        xor a		; Keep at same place.
        ld (music_playing),a
        ret

.19:    cp -3           ; Repeat music?
        jp nz,.0
        ld hl,(music_start)
        jr .15

.0:     ld a,(music_timing)
        and $3f         ; Restart note time.
        ld (music_note_counter),a
        ld a,b
        cp $3f          ; Sustain?
        jr z,.1
        rlca
        rlca
        and 3
        ld (music_instrument_1),a    
        ld a,b
        and $3f
        ld (music_note_1),a    
        xor a         
        ld (music_counter_1),a    
.1:     ld a,c          
        cp $3f          
        jr z,.2
        rlca
        rlca
        and 3
        ld (music_instrument_2),a    
        ld a,c
        and $3f
        ld (music_note_2),a    
        xor a         
        ld (music_counter_2),a    
.2:     ld a,d          
        cp $3f          
        jr z,.3
        rlca
        rlca
        and 3
        ld (music_instrument_3),a    
        ld a,d
        and $3f
        ld (music_note_3),a    
        xor a         
        ld (music_counter_3),a    
.3:     ld a,e          
        ld (music_drum),a
        xor a
        ld (music_counter_4),a
        inc hl
        inc hl
        inc hl
        ld a,(music_timing)
        and $c0
        jr nz,.14
        inc hl
.14:    ld (music_pointer),hl

.6:     ld a,(music_note_1)    
        or a            
        jr z,.7         
        ld bc,(music_instrument_1)
        call music_note2freq
        ld (audio_freq1),hl 
        ld (audio_vol1),a

.7:     ld a,(music_note_2)    
        or a            
        jr z,.8         
        ld bc,(music_instrument_2)
        call music_note2freq
        ld (audio_freq2),hl 
        ld (audio_vol2),a

.8:     ld a,(music_note_3)    
        or a            
        jr z,.9         
        ld bc,(music_instrument_3)
        call music_note2freq
        ld (audio_freq3),hl 
        ld (audio_vol3),a

.9:     ld a,(music_drum)    
        or a            
        jr z,.4         
        dec a           ; 1 - Long drum.
        jr nz,.5
        ld a,(music_counter_4)
        cp 3
        jp nc,.4
.10:    ld a,5
        ld (audio_noise),a
        call enable_drum
        jr .4

.5:     dec a           ; 2 - Short durm.
        jr nz,.11
        ld a,(music_counter_4)
        or a
        jp nz,.4
        ld a,8
        ld (audio_noise),a
        call enable_drum
        jr .4

.11:    ;dec a           ; 3 - Roll.
        ;jp nz,.4
        ld a,(music_timing)
        and $3e
        rrca
        ld b,a
        ld a,(music_counter_4)
        cp 2
        jp c,.10
        cp b
        jp c,.4
        dec a
        dec a
        cp b
        jp c,.10
.4:
        ld a,(music_counter_1)
        inc a
        cp $18
        jp nz,$+5
        sub $08
        ld (music_counter_1),a

        ld a,(music_counter_2)
        inc a
        cp $18
        jp nz,$+5
        sub $08
        ld (music_counter_2),a

        ld a,(music_counter_3)
        inc a
        cp $18
        jp nz,$+5
        sub $08
        ld (music_counter_3),a

        ld hl,music_counter_4
        inc (hl)
        ld hl,music_note_counter
        dec (hl)
        ret

        ;
        ; Converts note to frequency.
 	; Input:
	;   A = Note (1-62).
	;   B = Instrument counter.
	;   C = Instrument.
        ; Output:
	;   HL = Frequency.
	;   A = Volume.
	;
music_note2freq:
        add a,a
        ld e,a
        ld d,0
        ld hl,music_notes_table
        add hl,de
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a
        ld a,c
        or a
        jp z,music_piano
        dec a
        jp z,music_clarinet
        dec a
        jp z,music_flute
        ;
        ; Bass instrument.
        ;
music_bass:
        add hl,hl

        ;
        ; Piano instrument.
        ;
music_piano:
        ld a,b
        add a,.1&255
        ld c,a
        adc a,.1>>8
        sub c
        ld b,a
        ld a,(bc)
        ret

.1:
        db 12,11,11,10,10,9,9,8
        db 8,7,7,6,6,5,5,4
        db 4,4,5,5,4,4,3,3

        ;
        ; Clarinet instrument.
        ;
music_clarinet:
        ld a,b
        add a,.1&255
        ld c,a
        adc a,.1>>8
        sub c
        ld b,a
        ld a,(bc)
        ld e,a
        rlca
        sbc a,a
        ld d,a
        add hl,de
        srl h           
        rr l
        jp nc,.2
        inc hl
.2:     ld a,c
        add a,24
        ld c,a
	jr nc,$+3
	inc b
        ld a,(bc)
        ret

.1:
        db 0,0,0,0
        db -2,-4,-2,0
        db 2,4,2,0
        db -2,-4,-2,0
        db 2,4,2,0
        db -2,-4,-2,0

        db 13,14,14,13,13,12,12,12
        db 11,11,11,11,12,12,12,12
        db 11,11,11,11,12,12,12,12

        ;
        ; Flute instrument.
        ;
music_flute:
        ld a,b
        add a,.1&255
        ld c,a
        adc a,.1>>8
        sub c
        ld b,a
        ld a,(bc)
        ld e,a
        rlca
        sbc a,a
        ld d,a
        add hl,de
        ld a,c
        add a,24
        ld c,a
	jr nc,$+3
	inc b
        ld a,(bc)
        ret

.1:
        db 0,0,0,0
        db 0,1,2,1
        db 0,1,2,1
        db 0,1,2,1
        db 0,1,2,1
        db 0,1,2,1
                 
        db 10,12,13,13,12,12,12,12
        db 11,11,11,11,10,10,10,10
        db 11,11,11,11,10,10,10,10

        ;
        ; Emit sound.
        ;
music_hardware:
    if COLECO+SG1000+SMS+SORD+MEMOTECH+PV2000
	ld a,(music_mode)
	cp 4		; PLAY SIMPLE?
	jr c,.7		; Yes, jump.
        ld a,(audio_vol2)
        or a
        jp nz,.7
        ld a,(audio_vol3)
        or a
        jp z,.7
        ld (audio_vol2),a
        xor a
        ld (audio_vol3),a
        ld hl,(audio_freq3)
        ld (audio_freq2),hl
.7:
        ld hl,(audio_freq1)
        ld a,h
        cp 4
        ld a,$9f
        jp nc,.1
        ld a,l
        and $0f
        or $80
        out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld a,h
        out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
        ld a,(audio_vol1)
        add a,ay2sn&255
        ld l,a
        adc a,ay2sn>>8
        sub l
        ld h,a
        ld a,(hl)
        or $90
.1:     out (PSG),a
      if MEMOTECH
	in a,($03)
      endif

        ld hl,(audio_freq2)
        ld a,h
        cp 4
        ld a,$bf
        jp nc,.2
        ld a,l
        and $0f
        or $a0
        out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld a,h
        out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
        ld a,(audio_vol2)
        add a,ay2sn&255
        ld l,a
        adc a,ay2sn>>8
        sub l
        ld h,a
        ld a,(hl)
        or $b0
.2:     out (PSG),a
      if MEMOTECH
	in a,($03)
      endif

	ld a,(music_mode)
	cp 4		; PLAY SIMPLE?
	jr c,.6		; Yes, jump.

        ld hl,(audio_freq3)
        ld a,h
        cp 4
        ld a,$df
        jp nc,.3
        ld a,l
        and $0f
        or $c0
        out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld a,h
        out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
        ld a,(audio_vol3)
        add a,ay2sn&255
        ld l,a
        adc a,ay2sn>>8
        sub l
        ld h,a
        ld a,(hl)
        or $d0
.3:     out (PSG),a
      if MEMOTECH
	in a,($03)
      endif

.6:
	ld a,(music_mode)
	and 1		; NO DRUMS?
	ret z		; Yes, return.

        ld a,(audio_vol4hw)
        inc a           
        jr z,.4        
        ld a,(audio_noise)
        cp 16
        ld b,$ec        
        jp c,.5
        ld b,$ed        
;       ld b,$ee        
.5:     ld a,(audio_control)
        cp b
        jr z,.4
        ld a,b
        ld (audio_control),a
        out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
.4:     ld a,(audio_vol4hw)
        out (PSG),a
      if MEMOTECH
	in a,($03)
      endif
        ret
    endif
    if MSX+SVI+EINSTEIN+NABU
	ld a,(music_mode)
	cp 4		; PLAY SIMPLE?
	jr c,.8		; Yes, jump.	
	ld hl,audio_freq1
	ld bc,$0b00
	ld a,c
	ld e,(hl)
	call WRTPSG
	inc hl
	inc c
	djnz $-7
	ret
.8:
	ld hl,audio_freq1
	ld bc,$0400
	ld a,c
	ld e,(hl)
	call WRTPSG
	inc hl
	inc c
	djnz $-7
	inc hl
	inc hl
	inc c
	inc c
	ld a,(music_mode)
	and 1
	jr z,.9
	ld a,c
	ld e,(hl)
	call WRTPSG
	inc hl
	inc c
	ld a,c
	ld e,(hl)
	call WRTPSG
	inc hl
	inc c
	jr .10
.9:	inc hl
	inc c
	inc hl
	inc c
.10:	ld b,$02
	ld a,c
	ld e,(hl)
	call WRTPSG
	inc hl
	inc c
	djnz $-7
	ret
    endif

        ;
        ; Enable drum.
        ;
enable_drum:
    if COLECO+SG1000+SMS+SORD+MEMOTECH+PV2000
        ld a,$f5
        ld (audio_vol4hw),a
    else
        ld hl,audio_mix
        ld a,(audio_vol2)
        or a
        jr nz,.1
        ld a,10
        ld (audio_vol2),a
        set 1,(hl)
.1:     res 4,(hl)
    endif
        ret

        ;
	; Musical notes table.
	;
music_notes_table:
        ; Silence - 0
        dw 0
    if MEMOTECH+EINSTEIN
	; Values for 4.00 mhz.
	; 2nd octave - Index 1
	dw 1911,1804,1703,1607,1517,1432,1351,1276,1204,1136,1073,1012
	; 3rd octave - Index 13
	dw 956,902,851,804,758,716,676,638,602,568,536,506
	; 4th octave - Index 25
	dw 478,451,426,402,379,358,338,319,301,284,268,253
	; 5th octave - Index 37
	dw 239,225,213,201,190,179,169,159,150,142,134,127
	; 6th octave - Index 49
	dw 119,113,106,100,95,89,84,80,75,71,67,63
	; 7th octave - Index 61
	dw 60,56,53
    else
	; Values for 3.58 mhz.
	; 2nd octave - Index 1
	dw 1710,1614,1524,1438,1357,1281,1209,1141,1077,1017,960,906
	; 3rd octave - Index 13
	dw 855,807,762,719,679,641,605,571,539,508,480,453
	; 4th octave - Index 25
	dw 428,404,381,360,339,320,302,285,269,254,240,226
	; 5th octave - Index 37
	dw 214,202,190,180,170,160,151,143,135,127,120,113
	; 6th octave - Index 49
	dw 107,101,95,90,85,80,76,71,67,64,60,57
	; 7th octave - Index 61
	dw 53,50,48
    endif

    if COLECO+SG1000+SMS+SORD+MEMOTECH+PV2000
        ;
        ; Converts AY-3-8910 volume to SN76489
        ;
ay2sn:
        db $0f,$0f,$0f,$0e,$0e,$0e,$0d,$0b,$0a,$08,$07,$05,$04,$03,$01,$00
    endif

music_silence:
	db 8
	db 0,0,0,0
	db -2
    endif

    if CVBASIC_COMPRESSION
define_char_unpack:
	ex de,hl
	pop af
	pop hl
	push af
	add hl,hl	; x2
	add hl,hl	; x4
	add hl,hl	; x8
    if SMS
	add hl,hl       ; x16
	add hl,hl       ; x32
    endif
	ex de,hl
    if SMS
    else
	ld a,(mode)
	and 8
	jp z,unpack3
    endif
	jp unpack

    if SMS
    else
define_color_unpack:
	ex de,hl
	pop af
	pop hl
	push af
	add hl,hl	; x2
	add hl,hl	; x4
	add hl,hl	; x8
	ex de,hl
	set 5,d
unpack3:
	call .1
	call .1
.1:
	push de
	push hl
	call unpack
	pop hl
	pop de
	ld a,d
	add a,8	
	ld d,a
	ret
    endif
	
        ;
        ; Pletter-0.5c decompressor (XL2S Entertainment & Team Bomba)
        ;
unpack:
; Initialization
        ld a,(hl)
        inc hl
	exx
        ld de,0
        add a,a
        inc a
        rl e
        add a,a
        rl e
        add a,a
        rl e
        rl e
        ld hl,.modes
        add hl,de
        ld c,(hl)
        inc hl
        ld b,(hl)
        push bc
        pop ix
        ld e,1
	exx
        ld iy,.loop

; Main depack loop
.literal:
        ex af,af'
        call nmi_off
        ld a,(hl)
        ex de,hl
        call WRTVRM
        ex de,hl
        inc hl
        inc de
        call nmi_on
        ex af,af'
.loop:   add a,a
        call z,.getbit
        jr nc,.literal

; Compressed data
	exx
        ld h,d
        ld l,e
.getlen: add a,a
        call z,.getbitexx
        jr nc,.lenok
.lus:    add a,a
        call z,.getbitexx
        adc hl,hl
        ret c   
        add a,a
        call z,.getbitexx
        jr nc,.lenok
        add a,a
        call z,.getbitexx
        adc hl,hl
        ret c  
        add a,a
        call z,.getbitexx
        jr c,.lus
.lenok:  inc hl
	exx
        ld c,(hl)
        inc hl
        ld b,0
        bit 7,c
        jr z,.offsok
        jp (ix)

.mode6:  add a,a
        call z,.getbit
        rl b
.mode5:  add a,a
        call z,.getbit
        rl b
.mode4:  add a,a
        call z,.getbit
        rl b
.mode3:  add a,a
        call z,.getbit
        rl b
.mode2:  add a,a
        call z,.getbit
        rl b
        add a,a
        call z,.getbit
        jr nc,.offsok
        or a
        inc b
        res 7,c
.offsok: inc bc
        push hl
	exx
        push hl
	exx
        ld l,e
        ld h,d
        sbc hl,bc
        pop bc
        ex af,af'
.loop2: 
        call nmi_off
        call RDVRM              ; unpack
        ex de,hl
        call WRTVRM
        ex de,hl        ; 4
        call nmi_on
        inc hl          ; 6
        inc de          ; 6
        dec bc          ; 6
        ld a,b          ; 4
        or c            ; 4
        jr nz,.loop2     ; 10
        ex af,af'
        pop hl
        jp (iy)

.getbit: ld a,(hl)
        inc hl
	rla
	ret

.getbitexx:
	exx
        ld a,(hl)
        inc hl
	exx
	rla
	ret

.modes:
        dw      .offsok
        dw      .mode2
        dw      .mode3
        dw      .mode4
        dw      .mode5
        dw      .mode6

    endif

START:
    if SVI+SG1000+SMS
	im 1
    endif
    if MEMOTECH
Z80_CTC:	equ $08
    endif
    if EINSTEIN
Z80_CTC:	equ $28
    endif

    if EINSTEIN+MEMOTECH
	di
	im 2
	ld a,rom_start>>8
	ld i,a
	ld a,$03	; Reset Z80 CTC
	out (Z80_CTC+0),a
	out (Z80_CTC+1),a
	out (Z80_CTC+2),a
	out (Z80_CTC+3),a
	out (Z80_CTC+0),a
	out (Z80_CTC+1),a
	out (Z80_CTC+2),a
	out (Z80_CTC+3),a
	ld a,$08	; Interrupt vector offset
	out (Z80_CTC+0),a	
    endif
    if MEMOTECH
	ld a,$25	; Disable channel 2 interrupt.
	out (Z80_CTC+2),a
	ld a,$9c
	out (Z80_CTC+2),a
	ld a,$25	; Disable channel 1 interrupt.
	out (Z80_CTC+1),a
	ld a,$9c
	out (Z80_CTC+1),a
	ld a,$c5	; Enable channel 0 interrupt (VDP).
	out (Z80_CTC+0),a
	ld a,$01
	out (Z80_CTC+0),a
    endif
    if SORD
	ld hl,$186c	; Disable handling of CTC Channel 1 interruption.
	ld ($7002),hl
	ld hl,$7040
	set 0,(hl)	; Avoid BIOS VDP handling.
    endif
    if SVI
	ld e,$00
	ld a,$08
	call WRTPSG
	ld a,$09
	call WRTPSG
	ld a,$0A
	call WRTPSG
	ld a,$07
	ld e,$b8
	call WRTPSG
	ld a,$92	; Setup 8255 for keyboard/joystick reading.
	out ($97),a
    endif
    if SG1000+SMS
	; Contributed by SiRioKD
	ld a,$9F	; Turn off PSG
	out (PSG),a
	ld a,$BF	
	out (PSG),a
	ld a,$DF
	out (PSG),a
	ld a,$FF
	out (PSG),a	
	ld a,$92	; Setup 8255 for SC3000.
	out ($df),a
    endif
    if SG1000+SMS+SVI
	; Wait for VDP ready (around 1000 ms)
	ld b,11
	ld de,$FFFF
.delay1:
	ld hl,$39DE
.delay2:
	add hl,de
	jr c,.delay2
	djnz .delay1
    else
	di
	ld sp,STACK
    endif
    if NABU
	im 2
	ld a,nabu_int>>8
	ld i,a
	ld a,$03	; Disable ROM
	out ($00),a
	ld a,$07
	out ($41),a
	ld a,$78	; Setup output ports and mixer.
	out ($40),a
	ld a,$0e
	out ($41),a
	ld a,$30	; Enable video and keyboard.
	out ($40),a
    endif
    if PV2000
	ld hl,nmi_handler
	ld ($7499),hl
	ld a,(VDPR+1)
	ld bc,$8201
	call WRTVDP
	ld a,(VDPR+1)
	ld bc,$8201
	call WRTVDP
    else
	in a,(VDPR+1)
	ld bc,$8201
	call WRTVDP
	in a,(VDPR+1)
	ld bc,$8201
	call WRTVDP
    endif
  if CVBASIC_BANK_SWITCHING
    if COLECO
	ld a,($ffc0)	; Megacart
    endif
    if SG1000+SMS
        ld a,1		; Sega mapper
        ld ($fffe),a
    endif
    if MSX
        ld a,1		; ASCII 16K
        ld ($7000),a
    endif
  endif
    if MEMOTECH+EINSTEIN+NABU
	ld ix,(lfsr)
	ld hl,ram_start
	ld de,ram_start+1
	ld bc,ram_end-ram_start-1
	ld (hl),0
	ldir
	ld (lfsr),ix
    endif
    if MSX+SVI+SMS
	ld ix,(lfsr)
	ld hl,BASE_RAM
	ld de,BASE_RAM+1
	ld bc,RAM_SIZE-1
	ld (hl),0
	ldir
	ld (lfsr),ix
    endif
    if COLECO+SG1000+SORD+PENCIL+PV2000
	ld hl,(lfsr)	; Save RAM trash for random generator.
	ld de,BASE_RAM
	xor a
	ld (de),a
	inc de
      if PV2000
        bit 7,d		; 2.5K of RAM
      endif
      if PENCIL
        bit 3,d		; 2K of RAM
      endif
      if COLECO+SG1000+SORD
	bit 2,d		; 1K of RAM
      endif
	jp z,$-4
	ld (lfsr),hl
    endif

    if SGM
WRITE_REGISTER:	equ $1fd9
FILL_VRAM:	equ $1f82
WRITE_VRAM:	equ $1fdf

        ld b,$00	; First step.
.0:     ld hl,$2000	; RAM at $2000.
.1:     ld (hl),h	; Try to write a byte.
        inc h
        jp p,.1		; Repeat until reaching $8000.
        ld h,$20	; Go back at $2000.
.2:     ld a,(hl)	; Read back byte.
        cp h		; Is it correct?
        jr nz,.3	; No, jump.
        inc h
        jp p,.2		; Repeat until reaching $8000.
        jp .4		; Memory valid!

.3:     ld a,$01        ; Enable SGM
        out ($53),a
        inc b
        bit 1,b         ; Already enabled?
        jr z,.0		; No, test RAM again.

        ld bc,$0000
        call WRITE_REGISTER
        ld bc,$0180
        call WRITE_REGISTER
        ld bc,$0206
        call WRITE_REGISTER
        ld bc,$0380
        call WRITE_REGISTER
        ld bc,$0400
        call WRITE_REGISTER
        ld bc,$0536
        call WRITE_REGISTER
        ld bc,$0607
        call WRITE_REGISTER
        ld bc,$070D
        call WRITE_REGISTER
        ld bc,$03F0
        ld de,$00E8 
        ld hl,$158B     ; Note! direct access to Colecovision ROM
        call WRITE_VRAM
        ld hl,$2000
        ld de,32 
        ld a,$FD
        call FILL_VRAM
        ld hl,$1B00
        ld de,128
        ld a,$D1
        call FILL_VRAM
        ld hl,$1800
        ld de,769
        ld a,$20
        call FILL_VRAM
        ld bc,$0020
        ld de,$1980 
        ld hl,.5
        call WRITE_VRAM
        ld bc,$01C0
        call WRITE_REGISTER
        jr $

.5:     db " SUPER GAME MODULE NOT DETECTED "

.4:
	ld ix,(lfsr)
        ld hl,$2000
        ld de,$2001
        ld bc,$5FFF
        ld (hl),0
        ldir
	ld (lfsr),ix
    endif
    if COLECO
	ld a,($0069)
	cp 50
	ld a,1
	jr nz,$+3
	dec a
    endif
    if SG1000+SMS+SVI+SORD+PV2000+NABU
	ld a,1		; Always NTSC.
    endif
    if MEMOTECH+EINSTEIN
	ld a,0		; Always PAL.
    endif
    if MSX
        call RSLREG
        ld b,1          ; $4000-$7fff
        call get_slot_mapping
        ld h,$80
        call ENASLT     ; Map into $8000-$BFFF

	ld a,($002b)
	cpl
	rlca
	and $01
    endif
	ld (ntsc),a

	call music_init

	xor a
	ld (mode),a
    if SMS
	call mode_4
    else
	call mode_0
    endif

	xor a
	ld (joy1_data),a
	ld (joy2_data),a
    if NABU
        ld (nabu_data0),a
        ld (nabu_data1),a
    endif
	ld a,$0f
	ld (key1_data),a
	ld (key2_data),a
    if NABU
        ld (nabu_data2),a
    endif

    if MSX
	ld hl,nmi_handler
	ld ($fd9b),hl
	ld a,$c3
	ld ($fd9a),a
    endif
    if SORD
	ld hl,nmi_handler
	ld ($7006),hl
    endif

	; CVBasic program start.

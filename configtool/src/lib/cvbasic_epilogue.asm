	;
	; CVBasic epilogue (BASIC compiler for Colecovision)
	;
	; by Oscar Toledo G.
	; https://nanochess.org/
	;
	; Creation date: Feb/27/2024.
	; Revision date: Feb/29/2024. Added joystick, keypad, frame, random, and
	;                             read_pointer variables.
	; Revision date: Mar/04/2024. Added music player.
	; Revision date: Mar/05/2024. Added support for Sega SG1000.
	; Revision date: Mar/12/2024. Added support for MSX.
	; Revision date: Mar/13/2024. Added Pletter decompressor.
	; Revision date: Mar/19/2024. Added support for sprite flicker.
	; Revision date: Apr/11/2024. Added support for Super Game Module.
	; Revision date: Apr/13/2024. Updates LFSR in interruption handler.
	; Revision date: Apr/26/2024. All code moved to cvbasic_prologue.asm so it
	;                             can remain accessible in bank 0 (bank switching).
	; Revision date: Aug/02/2024. Added rom_end label for Memotech.
	; Revision date: Aug/15/2024. Added support for Tatung Einstein.
	; Revision date: Nov/12/2024. Added vdp_status.
	; Revision date: Feb/03/2025. Round final ROM size to 8K multiples.
	;

rom_end:

	; ROM final size rounding
    if MSX+COLECO+SG1000+SMS+SVI+SORD
        TIMES (($+$1FFF)&$1e000)-$ DB $ff
    endif
    if MEMOTECH+EINSTEIN+NABU
	; Align following data to a 256-byte page.
        TIMES $100-($&$ff) DB $4f
    endif
    if PV2000
	TIMES $10000-$ DB $ff
    endif
    if SG1000+SMS
      if CVBASIC_BANK_SWITCHING
        forg CVBASIC_BANK_ROM_SIZE*1024-1	; Force final ROM size
	db $ff
      endif
	forg $7FF0
	org $7FF0
	db "TMR SEGA"
	db 0,0
	db 0,0		; Checksum
	db $11,$78	; Product code
	db $00		; Version
	db $4c		; SMS Export + 32KB for checksum
    endif
    if COLECO+SG1000+SMS+MSX+SVI+SORD+PV2000
	org BASE_RAM
    endif
ram_start:

sprites:
    if SMS
	rb 256
    else
	rb 128
    endif
sprite_data:
	rb 4
frame:
	rb 2
read_pointer:
	rb 2
cursor:
	rb 2
lfsr:
	rb 2
mode:
	rb 1
flicker:
	rb 1
joy1_data:
	rb 1
joy2_data:
	rb 1
key1_data:
	rb 1
key2_data:
	rb 1
ntsc:
	rb 1
vdp_status:
	rb 1
    if NABU
nabu_data0: rb 1
nabu_data1: rb 1
nabu_data2: rb 1
    endif

    if CVBASIC_MUSIC_PLAYER
music_tick:             rb 1
music_mode:             rb 1

    if CVBASIC_BANK_SWITCHING
music_bank:             rb 1
    endif
music_start:		rb 2
music_pointer:		rb 2
music_playing:		rb 1
music_timing:		rb 1
music_note_counter:	rb 1
music_instrument_1:	rb 1
music_counter_1:	rb 1
music_note_1:		rb 1
music_instrument_2:	rb 1
music_counter_2:	rb 1
music_note_2:		rb 1
music_instrument_3:	rb 1
music_counter_3:	rb 1
music_note_3:		rb 1
music_counter_4:	rb 1
music_drum:		rb 1

audio_freq1:		rb 2
audio_freq2:		rb 2
audio_freq3:		rb 2
audio_noise:		rb 1
audio_mix:		rb 1
audio_vol1:		rb 1
audio_vol2:		rb 1
audio_vol3:		rb 1

audio_control:		rb 1
audio_vol4hw:		rb 1
    endif

    if SGM
	org $2000	; Start for variables.
    endif

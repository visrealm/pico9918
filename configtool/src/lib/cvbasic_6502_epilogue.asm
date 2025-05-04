rom_end:
	times $bfe8-$ db $ff

	dw START
	dw 0		; IRQ2 handler.

	dw 0
	dw 0

	; Initial VDP registers
	db $02
	db $82
	db $06
	db $ff
	db $00
	db $36
	db $07
	db $01

	dw 0
	dw 0
	dw BIOS_NMI_RESET_ADDR	; Handler for reset.
	dw int_handler	; IRQ1 handler.

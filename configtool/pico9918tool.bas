'
' Project: pico9918tool
'
' Copyright (c) 2024 Troy Schrapel
'
' This code is licensed under the MIT license
'
' https://github.com/visrealm/pico9918
'


    DEFINE CHAR 128, 39, logo
    
    FOR I = 32 TO 128
        DEFINE COLOR I, 1, blue
    NEXT I
    FOR I = 128 TO 168
        DEFINE COLOR I, 1, white
    NEXT I

    DEFINE VRAM $1800, 19, logoNames
    DEFINE VRAM $1820, 19, logoNames2
    
    PRINT AT 26 + 32,"v1.0.0"
    PRINT AT 20,"CONFIGURATOR"
    
    FOR I = 64 TO 95
        VPOKE $1800 + I, 166
        VPOKE $1800 + 640 + I, 166
    NEXT I
    

    PRINT AT 64+ 36 + 64, "DETECTING..."
    PRINT AT 768-32, "    (C) 2024 Troy Schrapel"
    
    GOSUB vdp_detect
    
    isPico9918 = 0
    IF isF18ACompatible THEN
        PRINT AT 64+ 36 + 64, "DETECTED F18A COMPATIBLE"
        
        VDP(1) = $C2
        
        VDP(15) = 1
        statReg = USR RDVST
        VDP(15) = 14
        verReg = USR RDVST
        VDP(15) = 0
        VDP(1) = $E2
        verMaj = (verReg AND $f0) / 16
        verMin = verReg AND $0f
        IF (statReg AND $E8) = $E8 THEN
            PRINT AT 64+ 40 + 128, "PICO9918 ver ", verMaj, ".", verMin
            isPico9918 = 1
        ELSEIF (statReg AND $E0) = $E0 THEN
            PRINT AT 64+ 38 + 128, "GENUINE F18A ver ", verMaj, ".", verMin
        ELSE
            PRINT AT 64+ 40 + 128, "UNKNOWN SR1 = ", <>statReg
        END IF
        
    ELSE
        PRINT AT 64+ 36 + 64, " DETECTED TMS9918A VDP"
    END IF
    
    IF NOT isF18ACompatible THEN
        PRINT AT 64+ 38 + 256, "UNABLE TO CONFIGURE"
    ELSE
		PRINT AT 64 + 2 + 256,    "> 1. SCANLINES      : ON"
		PRINT AT 64 + 4+ 256 + 32,  "2. MAX SL SPRITES : 4"
		PRINT AT 64 + 4+ 256 + 64,  "3. CLOCK          : 352 Mhz"
		PRINT AT 64 + 4+ 256 + 96,  "4. DIAGNOSTICS    : ON"
    END IF
    
exit:
    WAIT
    GOTO exit

vdp_detect: PROCEDURE
    GOSUB vdp_unlock
    DEFINE VRAM $3F00, 6, vdp_gpu_detect
    VDP($36) = $3F                       ' set gpu start address msb
    VDP($37) = $00                       ' set gpu start address lsb (triggers)
    isF18ACompatible = VPEEK($3F00)=0    ' check result
    END
    
    
vdp_unlock: PROCEDURE
    VDP(1) = $C2                        ' disable interrupts
    VDP(57) = $1C                        ' unlock
    VDP(57) = $1C                       ' unlock... again
    VDP(1) = $E2                        ' enable interrupts
    END

vdp_gpu_detect:
    ' TMS9900 machine code (for GPU) to write $00 to VDP $3F00
    DATA BYTE $04, $E0    ' CLR  @>3F00
    DATA BYTE $3F, $00
    DATA BYTE $03, $40    ' IDLE    


' PICO9918 logo pattern
logo:
    DATA BYTE $1f,$3f,$7f,$ff,$00,$00,$00,$00
    DATA BYTE $ff,$ff,$ff,$ff,$03,$01,$01,$03
    DATA BYTE $03,$c3,$e3,$f3,$f3,$f3,$f3,$f3
    DATA BYTE $e0,$e0,$e1,$e3,$e3,$e7,$e7,$e7
    DATA BYTE $1f,$7f,$ff,$ff,$f8,$e0,$c0,$c0
    DATA BYTE $ff,$fe,$fc,$f8,$00,$00,$00,$00
    DATA BYTE $00,$03,$0f,$1f,$1f,$3f,$3e,$3e
    DATA BYTE $7f,$ff,$ff,$ff,$c0,$00,$00,$00
    DATA BYTE $80,$f0,$fc,$fe,$fe,$3f,$1f,$1f
    DATA BYTE $07,$18,$20,$20,$41,$42,$41,$20
    DATA BYTE $ff,$00,$00,$00,$ff,$00,$ff,$00
    DATA BYTE $80,$60,$10,$08,$05,$85,$85,$04
    DATA BYTE $1f,$60,$80,$80,$07,$08,$07,$80
    DATA BYTE $fe,$01,$00,$00,$fc,$02,$fe,$00
    DATA BYTE $00,$81,$42,$24,$17,$10,$10,$10
    DATA BYTE $fc,$04,$04,$04,$84,$84,$84,$84
    DATA BYTE $1f,$60,$80,$80,$83,$84,$43,$20
    DATA BYTE $ff,$00,$00,$00,$fc,$02,$fc,$00
    DATA BYTE $80,$60,$10,$10,$10,$10,$20,$40
    DATA BYTE $ff,$ff,$ff,$ff,$f8,$f8,$f8,$f8
    DATA BYTE $ff,$ff,$ff,$fe,$00,$00,$00,$00
    DATA BYTE $f3,$e3,$c3,$03,$03,$03,$03,$03
    DATA BYTE $e7,$e7,$e7,$e3,$e3,$e1,$e0,$e0
    DATA BYTE $c0,$c0,$e0,$f8,$ff,$ff,$7f,$1f
    DATA BYTE $00,$00,$00,$00,$ff,$fe,$fc,$f8
    DATA BYTE $3e,$3e,$3f,$1f,$1f,$0f,$03,$00
    DATA BYTE $00,$00,$00,$c0,$ff,$ff,$ff,$ff
    DATA BYTE $1f,$1f,$3f,$fe,$fe,$fc,$f0,$80
    DATA BYTE $18,$07,$00,$07,$08,$10,$20,$3f
    DATA BYTE $00,$ff,$00,$ff,$00,$00,$00,$ff
    DATA BYTE $04,$84,$84,$08,$08,$10,$60,$80
    DATA BYTE $60,$1f,$00,$1f,$20,$40,$80,$ff
    DATA BYTE $00,$fe,$02,$fc,$00,$00,$01,$fe
    DATA BYTE $10,$10,$10,$20,$20,$40,$80,$00
    DATA BYTE $84,$84,$84,$84,$84,$84,$84,$fc
    DATA BYTE $40,$83,$84,$83,$80,$80,$60,$1f
    DATA BYTE $00,$fc,$02,$fc,$00,$00,$00,$ff
    DATA BYTE $20,$10,$10,$10,$10,$10,$60,$80

dash:
    DATA BYTE $00,$00,$00,$ff,$ff,$00,$00,$00


' PICO9918 logo name table entries (rows 1 and 2)
logoNames:
    DATA BYTE 128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146
logoNames2:
    DATA BYTE 147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165

' color entries for an entire tile
white:
    DATA BYTE $f0,$f0,$f0,$f0,$f0,$f0,$f0,$f0
grey:
    DATA BYTE $e0,$e0,$e0,$e0,$e0,$e0,$e0,$e0
blue:
    DATA BYTE $40,$40,$40,$40,$40,$40,$40,$40


font:
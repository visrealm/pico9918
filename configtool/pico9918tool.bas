'
' Project: pico9918tool
'
' Copyright (c) 2024 Troy Schrapel
'
' This code is licensed under the MIT license
'
' https://github.com/visrealm/pico9918
'

    CONST OPT_COUNT    = 4
    CONST OPT_NAME_LEN = 16
    CONST TRUE         = -1
    CONST FALSE        = 0
    
    optIdx = 0
    currentOptIdx = 0

    GOSUB setup_tiles    
    
    PRINT AT 26 + 32,"v1.0.0"
    PRINT AT 20,"CONFIGURATOR"
    
    FOR I = 64 TO 95
        VPOKE $1800 + I, 20
        VPOKE $1800 + 640 + I, 20
        VPOKE $1800 + 128 + I, 20
    NEXT I
    

    PRINT AT 768-32, "    (C) 2024 Troy Schrapel"
    
    GOSUB vdp_detect
    
    IF isF18ACompatible THEN
        
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
            PRINT AT 35 + 96, "DETECTED: PICO9918 ver ", verMaj, ".", verMin
            isPico9918 = TRUE
        ELSEIF (statReg AND $E0) = $E0 THEN
            PRINT AT 37 + 96, "DETECTED: F18A ver ", verMaj, ".", verMin
        ELSE
            PRINT AT 40 + 96, "DETECTED UNKNOWN SR1 = ", <>statReg
        END IF
        
    ELSE
        PRINT AT 38 + 96, "DETECTED: LEGACY VDP"
    END IF
    isPico9918 = TRUE
    IF NOT isPico9918 THEN
        PRINT AT 64+ 39 + 320, "PICO9918 NOT FOUND"
    ELSE
        GOSUB render_options
        WHILE 1
            WAIT
            dirty = 1
            
            key = CONT.KEY
            IF key >= $30 THEN key = key - $30
            
            IF CONT.DOWN AND currentOptIdx < (OPT_COUNT - 1) THEN
                currentOptIdx = currentOptIdx + 1
            ELSEIF CONT.UP AND currentOptIdx > 0 THEN
                currentOptIdx = currentOptIdx - 1
            ELSEIF key > 0 AND key <= OPT_COUNT THEN
                currentOptIdx = key - 1
            ELSE
                dirty = 0
            END IF
            
            IF dirty THEN
                WAIT
                GOSUB render_options
                GOSUB delay
                dirty = 0
            END IF
            
        WEND
    END IF
    
exit:
    WAIT
    GOTO exit
    
    

delay: PROCEDURE
    FOR del = 1 TO 10
        WAIT
    NEXT del
    END
    

setup_tiles: PROCEDURE
    VDP(1) = $82                        ' disable interrupts and display

    DEFINE CHAR 32, 96, font
    DEFINE CHAR 128 + 32, 96, font
    DEFINE CHAR 1, 19, logo
    DEFINE CHAR 129, 19, logo2

    DEFINE CHAR 20, 1, dash
    DEFINE CHAR 148, 1, dash
    
    FOR J = 148 TO 250
        DEFINE COLOR J, 1, inv_white
    NEXT J
    FOR I = 0 TO 147
        DEFINE COLOR I, 1, white
    NEXT I

    DEFINE VRAM $1800, 19, logoNames
    DEFINE VRAM $1820, 19, logoNames2
    
    END

render_options: PROCEDURE
    FOR optIdx = 0 TO OPT_COUNT - 1
        GOSUB render_opt
    NEXT optIdx
    END

render_opt: PROCEDURE
    PRINT AT 322 + (optIdx * 32), " ",optIdx + 1,". "
    DEFINE VRAM $1946 + (optIdx * 32), OPT_NAME_LEN, VARPTR options(optIdx * 18 + 1)
    PRINT AT 342 + (optIdx * 32), " : ", <.5>options((optIdx * 18) + 17)
    IF optIdx = currentOptIdx THEN
        FOR R = 0 TO 27
            C = VPEEK($1942 + (optIdx * 32) + R)
            C = C + 128
            VPOKE ($1942 + (optIdx * 32) + R), C
        NEXT R
    END IF
    END
    

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

options:
    DATA BYTE 0,"CRT SCANLINES   ",2
    DATA BYTE 1,"SCANLINE SPRITES",2
    DATA BYTE 2,"CLOCK FREQ.     ",4
    DATA BYTE 3,"DIAGNOSTICS     ",2


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
logo2:
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
    DATA BYTE 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19
logoNames2:
    DATA BYTE 129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147

' color entries for an entire tile
white:
    DATA BYTE $f0,$f0,$f0,$f0,$f0,$f0,$f0,$f0
grey:
    DATA BYTE $e0,$e0,$e0,$e0,$e0,$e0,$e0,$e0
blue:
    DATA BYTE $40,$40,$40,$40,$40,$40,$40,$40
inv_white:
    DATA BYTE $0f,$0f,$0f,$0f,$0f,$0f,$0f,$0f


font:
    DATA BYTE $00,$00,$00,$00,$00,$00,$00,$00 ' <SPACE$
    DATA BYTE $18,$18,$18,$18,$18,$00,$18,$00 ' !
    DATA BYTE $6C,$6C,$6C,$00,$00,$00,$00,$00 ' "
    DATA BYTE $6C,$6C,$FE,$6C,$FE,$6C,$6C,$00 ' #
    DATA BYTE $18,$7E,$C0,$7C,$06,$FC,$18,$00 ' $
    DATA BYTE $00,$C6,$CC,$18,$30,$66,$C6,$00 ' %
    DATA BYTE $38,$6C,$38,$76,$DC,$CC,$76,$00 ' &
    DATA BYTE $30,$30,$60,$00,$00,$00,$00,$00 ' '
    DATA BYTE $0C,$18,$30,$30,$30,$18,$0C,$00 ' (
    DATA BYTE $30,$18,$0C,$0C,$0C,$18,$30,$00 ' )
    DATA BYTE $00,$66,$3C,$FF,$3C,$66,$00,$00 ' *
    DATA BYTE $00,$18,$18,$7E,$18,$18,$00,$00 ' +
    DATA BYTE $00,$00,$00,$00,$00,$18,$18,$30 ' ,
    DATA BYTE $00,$00,$00,$7E,$00,$00,$00,$00 ' -
    DATA BYTE $00,$00,$00,$00,$00,$18,$18,$00 ' .
    DATA BYTE $06,$0C,$18,$30,$60,$C0,$80,$00 ' /
    DATA BYTE $7C,$CE,$DE,$F6,$E6,$C6,$7C,$00 ' 0
    DATA BYTE $18,$38,$18,$18,$18,$18,$7E,$00 ' 1
    DATA BYTE $7C,$C6,$06,$7C,$C0,$C0,$FE,$00 ' 2
    DATA BYTE $FC,$06,$06,$3C,$06,$06,$FC,$00 ' 3
    DATA BYTE $0C,$CC,$CC,$CC,$FE,$0C,$0C,$00 ' 4
    DATA BYTE $FE,$C0,$FC,$06,$06,$C6,$7C,$00 ' 5
    DATA BYTE $7C,$C0,$C0,$FC,$C6,$C6,$7C,$00 ' 6
    DATA BYTE $FE,$06,$06,$0C,$18,$30,$30,$00 ' 7
    DATA BYTE $7C,$C6,$C6,$7C,$C6,$C6,$7C,$00 ' 8
    DATA BYTE $7C,$C6,$C6,$7E,$06,$06,$7C,$00 ' 9
    DATA BYTE $00,$18,$18,$00,$00,$18,$18,$00 ' :
    DATA BYTE $00,$18,$18,$00,$00,$18,$18,$30 ' ;
    DATA BYTE $0C,$18,$30,$60,$30,$18,$0C,$00 ' <
    DATA BYTE $00,$00,$7E,$00,$7E,$00,$00,$00 ' =
    DATA BYTE $30,$18,$0C,$06,$0C,$18,$30,$00 ' >
    DATA BYTE $3C,$66,$0C,$18,$18,$00,$18,$00 ' ?
    DATA BYTE $7C,$C6,$DE,$DE,$DE,$C0,$7E,$00 ' @
    DATA BYTE $38,$6C,$C6,$C6,$FE,$C6,$C6,$00 ' A
    DATA BYTE $FC,$C6,$C6,$FC,$C6,$C6,$FC,$00 ' B
    DATA BYTE $7C,$C6,$C0,$C0,$C0,$C6,$7C,$00 ' C
    DATA BYTE $F8,$CC,$C6,$C6,$C6,$CC,$F8,$00 ' D
    DATA BYTE $FE,$C0,$C0,$F8,$C0,$C0,$FE,$00 ' E
    DATA BYTE $FE,$C0,$C0,$F8,$C0,$C0,$C0,$00 ' F
    DATA BYTE $7C,$C6,$C0,$C0,$CE,$C6,$7C,$00 ' G
    DATA BYTE $C6,$C6,$C6,$FE,$C6,$C6,$C6,$00 ' H
    DATA BYTE $7E,$18,$18,$18,$18,$18,$7E,$00 ' I
    DATA BYTE $06,$06,$06,$06,$06,$C6,$7C,$00 ' J
    DATA BYTE $C6,$CC,$D8,$F0,$D8,$CC,$C6,$00 ' K
    DATA BYTE $C0,$C0,$C0,$C0,$C0,$C0,$FE,$00 ' L
    DATA BYTE $C6,$EE,$FE,$FE,$D6,$C6,$C6,$00 ' M
    DATA BYTE $C6,$E6,$F6,$DE,$CE,$C6,$C6,$00 ' N
    DATA BYTE $7C,$C6,$C6,$C6,$C6,$C6,$7C,$00 ' O
    DATA BYTE $FC,$C6,$C6,$FC,$C0,$C0,$C0,$00 ' P
    DATA BYTE $7C,$C6,$C6,$C6,$D6,$DE,$7C,$06 ' Q
    DATA BYTE $FC,$C6,$C6,$FC,$D8,$CC,$C6,$00 ' R
    DATA BYTE $7C,$C6,$C0,$7C,$06,$C6,$7C,$00 ' S
    DATA BYTE $FF,$18,$18,$18,$18,$18,$18,$00 ' T
    DATA BYTE $C6,$C6,$C6,$C6,$C6,$C6,$FE,$00 ' U
    DATA BYTE $C6,$C6,$C6,$C6,$C6,$7C,$38,$00 ' V
    DATA BYTE $C6,$C6,$C6,$C6,$D6,$FE,$6C,$00 ' W
    DATA BYTE $C6,$C6,$6C,$38,$6C,$C6,$C6,$00 ' X
    DATA BYTE $C6,$C6,$C6,$7C,$18,$30,$E0,$00 ' Y
    DATA BYTE $FE,$06,$0C,$18,$30,$60,$FE,$00 ' Z
    DATA BYTE $3C,$30,$30,$30,$30,$30,$3C,$00 ' [
    DATA BYTE $C0,$60,$30,$18,$0C,$06,$02,$00 ' \
    DATA BYTE $3C,$0C,$0C,$0C,$0C,$0C,$3C,$00 ' ]
    DATA BYTE $10,$38,$6C,$C6,$00,$00,$00,$00 ' ^
    DATA BYTE $00,$00,$00,$00,$00,$00,$00,$FF ' _
    DATA BYTE $18,$18,$0C,$00,$00,$00,$00,$00 ' `
    DATA BYTE $00,$00,$7C,$06,$7E,$C6,$7E,$00 ' a
    DATA BYTE $C0,$C0,$C0,$FC,$C6,$C6,$FC,$00 ' b
    DATA BYTE $00,$00,$7C,$C6,$C0,$C6,$7C,$00 ' c
    DATA BYTE $06,$06,$06,$7E,$C6,$C6,$7E,$00 ' d
    DATA BYTE $00,$00,$7C,$C6,$FE,$C0,$7C,$00 ' e
    DATA BYTE $1C,$36,$30,$78,$30,$30,$78,$00 ' f
    DATA BYTE $00,$00,$7E,$C6,$C6,$7E,$06,$FC ' g
    DATA BYTE $C0,$C0,$FC,$C6,$C6,$C6,$C6,$00 ' h
    DATA BYTE $18,$00,$38,$18,$18,$18,$3C,$00 ' i
    DATA BYTE $06,$00,$06,$06,$06,$06,$C6,$7C ' j
    DATA BYTE $C0,$C0,$CC,$D8,$F8,$CC,$C6,$00 ' k
    DATA BYTE $38,$18,$18,$18,$18,$18,$3C,$00 ' l
    DATA BYTE $00,$00,$CC,$FE,$FE,$D6,$D6,$00 ' m
    DATA BYTE $00,$00,$FC,$C6,$C6,$C6,$C6,$00 ' n
    DATA BYTE $00,$00,$7C,$C6,$C6,$C6,$7C,$00 ' o
    DATA BYTE $00,$00,$FC,$C6,$C6,$FC,$C0,$C0 ' p
    DATA BYTE $00,$00,$7E,$C6,$C6,$7E,$06,$06 ' q
    DATA BYTE $00,$00,$FC,$C6,$C0,$C0,$C0,$00 ' r
    DATA BYTE $00,$00,$7E,$C0,$7C,$06,$FC,$00 ' s
    DATA BYTE $18,$18,$7E,$18,$18,$18,$0E,$00 ' t
    DATA BYTE $00,$00,$C6,$C6,$C6,$C6,$7E,$00 ' u
    DATA BYTE $00,$00,$C6,$C6,$C6,$7C,$38,$00 ' v
    DATA BYTE $00,$00,$C6,$C6,$D6,$FE,$6C,$00 ' w
    DATA BYTE $00,$00,$C6,$6C,$38,$6C,$C6,$00 ' x
    DATA BYTE $00,$00,$C6,$C6,$C6,$7E,$06,$FC ' y
    DATA BYTE $00,$00,$FE,$0C,$38,$60,$FE,$00 ' z
    DATA BYTE $0E,$18,$18,$70,$18,$18,$0E,$00 ' {
    DATA BYTE $18,$18,$18,$00,$18,$18,$18,$00 ' |
    DATA BYTE $70,$18,$18,$0E,$18,$18,$70,$00 ' }
    DATA BYTE $76,$DC,$00,$00,$00,$00,$00,$00 ' ~
    DATA BYTE $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF '  
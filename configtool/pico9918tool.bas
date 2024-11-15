'
' Project: pico9918tool
'
' Copyright (c) 2024 Troy Schrapel
'
' This code is licensed under the MIT license
'
' https://github.com/visrealm/pico9918
'

    CONST OPT_COUNT     = 6
    CONST OPT_NAME_LEN  = 16
    CONST TRUE          = -1
    CONST FALSE         = 0
    CONST #VDP_NAME_TAB = $1800
    CONST MENU_TOP      = 8
    CONST DASH          = 20

    DEF FN XY(X, Y) = ((Y) * 32 + (X))
    DEF FN NAME_TAB_XY(X, Y) = (#VDP_NAME_TAB + XY(X, Y))
    DEF FN PUT_XY(X, Y, C) = VPOKE NAME_TAB_XY(X, Y), C
    DEF FN GET_XY(X, Y) = VPEEK(NAME_TAB_XY(X, Y))
    
    optIdx = 0
    currentOptIdx = 0

    GOSUB setup_tiles
    GOSUB setup_header
    
    GOSUB vdp_detect

    BORDER 0
    
    IF isF18ACompatible THEN
        
        VDP(1) = $C2
        
        VDP(15) = 1
        statReg = USR RDVST
        VDP(15) = 14
        verReg = USR RDVST
        VDP(15) = 0
        VDP(1) = $E2
        verMaj = verReg / 16
        verMin = verReg AND $0f
        IF (statReg AND $E8) = $E8 THEN
            PRINT AT XY(3, 3), "Detected: PICO9918 ver ", verMaj, ".", verMin
            isPico9918 = TRUE
        ELSEIF (statReg AND $E0) = $E0 THEN
            PRINT AT XY(5, 3), "Detected: F18A ver  ."
            PUT_XY(5 + 19, 3, hexChar(verMaj))
            PUT_XY(5 + 21, 3, hexChar(verMin))
        ELSE
            PRINT AT XY(8, 3), "Detected UNKNOWN SR1 = ", <>statReg
        END IF
        
    ELSE
        PRINT AT XY(6, 3), "Detected: LEGACY VDP"
    END IF
    'isPico9918 = isF18ACompatible   ' FOR TESTING
    IF NOT isPico9918 THEN
        PRINT AT XY(7, 9 + (isF18ACompatible AND 3)), "PICO9918 not found"
        IF NOT isF18ACompatible THEN
            PRINT AT XY(15, 12), "OR"
            PRINT AT XY(3, 15), "PICO9918 firmware too old"
            PRINT AT XY(4, 17), "Firmware v1.0+ required"
            PRINT AT XY(4, 19), "Update manually via USB"
        END IF
    ELSE
        GOSUB update_palette
        PRINT AT XY(11, 6), "MAIN MENU"
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
    FOR del = 1 TO 5
        WAIT
    NEXT del
    END
    

setup_tiles: PROCEDURE
    VDP(1) = $82                        ' disable interrupts and display

    DEFINE CHAR 32, 96, font
    DEFINE CHAR 32 + 128, 96, font

    DEFINE CHAR 1, 19, logo
    DEFINE CHAR 1 + 128, 19, logo2

    DEFINE CHAR DASH, 1, dash
    DEFINE CHAR DASH + 128, 1, dash
    DEFINE CHAR 21, 1, round_left
    DEFINE CHAR 22, 1, round_right
    
    FOR I = 0 TO 31
        DEFINE COLOR I, 1, white
    NEXT I
    FOR I = 32 TO 127
        DEFINE COLOR I, 1, grey
    NEXT I
    FOR I = 128 TO 147
        DEFINE COLOR I, 1, white
    NEXT I
    FOR J = 148 TO 250
        DEFINE COLOR J, 1, inv_white
    NEXT J

    DEFINE COLOR DASH, 1, dash_c
    DEFINE COLOR 21, 1, highlight
    DEFINE COLOR 22, 1, highlight

    END

setup_header: PROCEDURE

    DEFINE VRAM NAME_TAB_XY(0, 0), 19, logoNames
    DEFINE VRAM NAME_TAB_XY(0, 1), 19, logoNames2

    PRINT AT XY(20, 0),"Configurator"
    PRINT AT XY(28, 1),"v1.0"
    PRINT AT XY(4, 23), "(C) 2024 Troy Schrapel"    

    FOR I = 0 TO 31
        PUT_XY(I, 2, DASH)
        PUT_XY(I, 4, DASH)
        PUT_XY(I, 22, DASH)
    NEXT I

    END

render_options: PROCEDURE
    FOR optIdx = 0 TO OPT_COUNT - 1
        GOSUB render_opt
    NEXT optIdx
    END

render_opt: PROCEDURE
    #ROWOFFSET = XY(0, MENU_TOP + optIdx)
    PRINT AT #ROWOFFSET + 2, " ",optIdx + 1,". "
    DEFINE VRAM #VDP_NAME_TAB + #ROWOFFSET + 6, OPT_NAME_LEN, VARPTR options(optIdx * 18 + 1)
    PRINT AT #ROWOFFSET + 22, " : ", <.4>options((optIdx * 18) + 17)," "
    IF optIdx = currentOptIdx THEN
        FOR R = 3 TO 28
            C = VPEEK(#VDP_NAME_TAB + #ROWOFFSET+ R)
            C = C OR 128
            VPOKE (#VDP_NAME_TAB + #ROWOFFSET + R), C
        NEXT R
        VPOKE (#VDP_NAME_TAB + #ROWOFFSET + 2), 21
        VPOKE (#VDP_NAME_TAB + #ROWOFFSET + 29), 22
    END IF
    END

update_palette: PROCEDURE    
    WAIT
    VDP(47) = $c0 + 2 ' palette data port fron index #2
    PRINT "\0\7"
    PRINT "\0\10"
    PRINT "\0\12"
    PRINT "\0\15"
    PRINT "\0\15"
    PRINT "\2\47"
    PRINT "\4\79"
    PRINT "\7\127"
    PRINT "\15\255"
    PRINT "\15\255"
    PRINT "\15\255"
    PRINT "\15\255"
    PRINT "\9\153"
    VDP(47) = $40
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
    VDP(57) = $1C                       ' unlock
    VDP(57) = $1C                       ' unlock... again
    VDP(1) = $E2                        ' enable interrupts
    END

' TMS9900 machine code (for PICO9918 GPU) to write $00 to VDP $3F00
vdp_gpu_detect:
    DATA BYTE $04, $E0    ' CLR  @>3F00
    DATA BYTE $3F, $00
    DATA BYTE $03, $40    ' IDLE    

options:
    DATA BYTE 0,"CRT scanlines   ",2
    DATA BYTE 1,"Scanline sprites",2
    DATA BYTE 2,"Clock freq.     ",4
    DATA BYTE 3,"Diagnostics     ",2
    DATA BYTE 4,"Default palette ",15
    DATA BYTE 5,"Reset defaults  ",15

hexChar:
    DATA BYTE "0123456789ABCDEF"

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
dash_c:
    DATA BYTE $00,$00,$00,$77,$44,$00,$00,$00
round_left:
    DATA BYTE $3F,$7F,$FF,$FF,$FF,$FF,$7F,$3F
round_right:
    DATA BYTE $FC,$FE,$FF,$FF,$FF,$FF,$FE,$FC


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
'    DATA BYTE $0f,$0f,$0f,$0f,$0f,$0f,$0f,$0f
'    DATA BYTE $f5,$f4,$f4,$f4,$f4,$f4,$f4,$f4
    DATA BYTE $f9,$f8,$f7,$f6,$f5,$f4,$f3,$f2
highlight:
    DATA BYTE $90,$80,$70,$60,$50,$40,$30,$20

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

palette:
    DATA BYTE $00,$00
    DATA BYTE $00,$00
    DATA BYTE $02,$C3
    DATA BYTE $05,$00'D6
    DATA BYTE $05,$4F
    DATA BYTE $07,$6F
    DATA BYTE $0D,$54
    DATA BYTE $04,$EF
    DATA BYTE $0F,$54
    DATA BYTE $0F,$76
    DATA BYTE $0D,$C3
    DATA BYTE $0E,$D6
    DATA BYTE $02,$B2
    DATA BYTE $0C,$5C
    DATA BYTE $08,$88
    DATA BYTE $0F,$FF
  
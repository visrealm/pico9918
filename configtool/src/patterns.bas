'
' Project: pico9918
'
' PICO9918 Configurator
'
' Copyright (c) 2024 Troy Schrapel
'
' This code is licensed under the MIT license
'
' https://github.com/visrealm/pico9918
'
' -----------------------------------------------------------------------------
' CVBasic source file. See: github.com/nanochess/CVBasic
' -----------------------------------------------------------------------------


' -----------------------------------------------------------------------------
' set up the various tile patters and colors
' -----------------------------------------------------------------------------
setupTiles: PROCEDURE
    VDP(1) = $82  ' disable interrupts and display

    DEFINE CHAR 32, 96, font        ' font standard
    DEFINE CHAR 32 + 128, 96, font  ' font highlighted

    DEFINE CHAR 1, 19, logo         ' first row of top left logo
    DEFINE CHAR 1 + 128, 19, logo2  ' second row of top left logo

    DEFINE CHAR PATT_IDX_BORDER_H, 6, lineSegments  '   border segments
    DEFINE CHAR PATT_IDX_BORDER_H + 128, 6, lineSegments
    DEFINE CHAR PATT_IDX_SLIDER, 1, sliderButton

    DEFINE CHAR PATT_IDX_BOX_TL, 4, palBox
    DEFINE CHAR PATT_IDX_BOX_TL + 128, 4, palBox

    DEFINE CHAR PATT_IDX_SELECTED_L, 1, highlightLeft   ' ends of selection bar
    DEFINE CHAR PATT_IDX_SELECTED_R, 1, highlightRight
    
    FOR I = 0 TO 31
        DEFINE COLOR I, 1, white    ' title color
    NEXT I
    FOR I = 32 TO 127
        DEFINE COLOR I, 1, grey     ' normal text color
    NEXT I
    FOR I = 128 TO 147
        DEFINE COLOR I, 1, white    ' title row 2 color
    NEXT I
    FOR I = 148 TO 250
        DEFINE COLOR I, 1, inv_white ' selected (highlighted) colors
    NEXT I

    DEFINE COLOR PATT_IDX_BORDER_H, 1, colorLineSegH     ' horizontal divideborder color
    FOR I = PATT_IDX_BORDER_V TO PATT_IDX_BORDER_BR
        DEFINE COLOR I, 1, colorLineSeg                  ' other border colors
    NEXT I    

    DEFINE COLOR PATT_IDX_BORDER_TL, 1, colorLineSegH                  
    DEFINE COLOR PATT_IDX_BORDER_TR, 1, colorLineSegH                  

    DEFINE COLOR PATT_IDX_BOX_TL + 128, 1, colorPalBoxSel
    DEFINE COLOR PATT_IDX_BOX_TR + 128, 1, colorPalBoxSel
    DEFINE COLOR PATT_IDX_BOX_BL + 128, 1, colorPalBoxSel2
    DEFINE COLOR PATT_IDX_BOX_BR + 128, 1, colorPalBoxSel2

    DEFINE COLOR PATT_IDX_SELECTED_L, 1, highlight    ' selection bar ends
    DEFINE COLOR PATT_IDX_SELECTED_R, 1, highlight
    DEFINE COLOR PATT_IDX_SLIDER, 1, highlight
    DEFINE COLOR PATT_IDX_SWATCH, 1, colorSwatch

    DEFINE SPRITE 0, 7, logoSprites  ' set up logo sprites used for 'scanline sprites' demo

    SPRITE FLICKER OFF
    END


' PICO9918 logo pattern
logo:
    DATA BYTE $1f, $3f, $7f, $ff, $00, $00, $00, $00
    DATA BYTE $ff, $ff, $ff, $ff, $03, $01, $01, $03
    DATA BYTE $03, $c3, $e3, $f3, $f3, $f3, $f3, $f3
    DATA BYTE $e0, $e0, $e1, $e3, $e3, $e7, $e7, $e7
    DATA BYTE $1f, $7f, $ff, $ff, $f8, $e0, $c0, $c0
    DATA BYTE $ff, $fe, $fc, $f8, $00, $00, $00, $00
    DATA BYTE $00, $03, $0f, $1f, $1f, $3f, $3e, $3e
    DATA BYTE $7f, $ff, $ff, $ff, $c0, $00, $00, $00
    DATA BYTE $80, $f0, $fc, $fe, $fe, $3f, $1f, $1f
    DATA BYTE $07, $18, $20, $20, $41, $42, $41, $20
    DATA BYTE $ff, $00, $00, $00, $ff, $00, $ff, $00
    DATA BYTE $80, $60, $10, $08, $05, $85, $85, $04
    DATA BYTE $1f, $60, $80, $80, $07, $08, $07, $80
    DATA BYTE $fe, $01, $00, $00, $fc, $02, $fe, $00
    DATA BYTE $00, $81, $42, $24, $17, $10, $10, $10
    DATA BYTE $fc, $04, $04, $04, $84, $84, $84, $84
    DATA BYTE $1f, $60, $80, $80, $83, $84, $43, $20
    DATA BYTE $ff, $00, $00, $00, $fc, $02, $fc, $00
    DATA BYTE $80, $60, $10, $10, $10, $10, $20, $40
logo2:
    DATA BYTE $ff, $ff, $ff, $ff, $f8, $f8, $f8, $f8
    DATA BYTE $ff, $ff, $ff, $fe, $00, $00, $00, $00
    DATA BYTE $f3, $e3, $c3, $03, $03, $03, $03, $03
    DATA BYTE $e7, $e7, $e7, $e3, $e3, $e1, $e0, $e0
    DATA BYTE $c0, $c0, $e0, $f8, $ff, $ff, $7f, $1f
    DATA BYTE $00, $00, $00, $00, $ff, $fe, $fc, $f8
    DATA BYTE $3e, $3e, $3f, $1f, $1f, $0f, $03, $00
    DATA BYTE $00, $00, $00, $c0, $ff, $ff, $ff, $ff
    DATA BYTE $1f, $1f, $3f, $fe, $fe, $fc, $f0, $80
    DATA BYTE $18, $07, $00, $07, $08, $10, $20, $3f
    DATA BYTE $00, $ff, $00, $ff, $00, $00, $00, $ff
    DATA BYTE $04, $84, $84, $08, $08, $10, $60, $80
    DATA BYTE $60, $1f, $00, $1f, $20, $40, $80, $ff
    DATA BYTE $00, $fe, $02, $fc, $00, $00, $01, $fe
    DATA BYTE $10, $10, $10, $20, $20, $40, $80, $00
    DATA BYTE $84, $84, $84, $84, $84, $84, $84, $fc
    DATA BYTE $40, $83, $84, $83, $80, $80, $60, $1f
    DATA BYTE $00, $fc, $02, $fc, $00, $00, $00, $ff
    DATA BYTE $20, $10, $10, $10, $10, $10, $60, $80

highlightLeft:
    DATA BYTE $3F, $7F, $FF, $FF, $FF, $FF, $7F, $3F
highlightRight:
    DATA BYTE $FC, $FE, $FF, $FF, $FF, $FF, $FE, $FC

lineSegments:
    DATA BYTE $00, $00, $00, $ff, $ff, $00, $00, $00
    DATA BYTE $18, $18, $18, $18, $18, $18, $18, $18 ' vert
    DATA BYTE $00, $00, $00, $ff, $fF, $3C, $18, $18 ' tl
    DATA BYTE $00, $00, $00, $ff, $Ff, $3c, $18, $18 ' tr
 '   DATA BYTE $00, $00, $00, $07, $0F, $1C, $18, $18 ' tl
'    DATA BYTE $00, $00, $00, $E0, $F0, $38, $18, $18 ' tr
    DATA BYTE $18, $18, $1C, $0F, $07, $00, $00, $00 ' bl
    DATA BYTE $18, $18, $38, $F0, $E0, $00, $00, $00 ' br
sliderButton:
    DATA BYTE $00, $3C, $7E, $FF, $FF, $7E, $3C, $00

colorLineSegH:
    DATA BYTE $00, $00, $00, $77, $44, $50, $50, $50
colorLineSeg:
    DATA BYTE $50, $50, $50, $50, $50, $50, $50, $50

palBox:
    DATA BYTE $3F, $51, $62, $44, $48, $51, $62, $44
    DATA BYTE $FC, $12, $22, $46, $8A, $12, $22, $46
    DATA BYTE $48, $51, $62, $44, $48, $51, $3F, $00
    DATA BYTE $8A, $12, $22, $46, $8A, $12, $FC, $00

horzBar:
    DATA BYTE PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H
    DATA BYTE PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H
    DATA BYTE PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H
    DATA BYTE PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H

' PICO9918 logo name table entries (rows 1 and 2)
logoNames:
    DATA BYTE 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19
logoNames2:
    DATA BYTE 129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147

' color entries for an entire tile
white: 
    DATA BYTE $f0, $f0, $f0, $f0, $f0, $f0, $f0, $f0
grey: 
    DATA BYTE $e0, $e0, $e0, $e0, $e0, $e0, $e0, $e0
blue: 
    DATA BYTE $40, $40, $40, $40, $40, $40, $40, $40
inv_white: 
    DATA BYTE $f9, $f8, $f7, $f6, $f5, $f4, $f3, $f2
highlight: 
    DATA BYTE $90, $80, $70, $60, $50, $40, $30, $20
colorPalBoxSel: 
    DATA BYTE $90, $90, $80, $80, $80, $70, $70, $70
colorPalBoxSel2: 
    DATA BYTE $60, $60, $60, $50, $50, $30, $30, $20
colorSwatch:
    DATA BYTE $0a, $0a, $0a, $0a, $0a, $0a, $0a, $0a

font:
    DATA BYTE $00, $00, $00, $00, $00, $00, $00, $00 ' <SPACE$
    DATA BYTE $18, $18, $18, $18, $18, $00, $18, $00 ' !
    DATA BYTE $6C, $6C, $6C, $00, $00, $00, $00, $00 ' "
    DATA BYTE $6C, $6C, $FE, $6C, $FE, $6C, $6C, $00 ' #
    DATA BYTE $18, $7E, $C0, $7C, $06, $FC, $18, $00 ' $
    DATA BYTE $00, $C6, $CC, $18, $30, $66, $C6, $00 ' %
    DATA BYTE $38, $6C, $38, $76, $DC, $CC, $76, $00 ' &
    DATA BYTE $30, $30, $60, $00, $00, $00, $00, $00 ' '
    DATA BYTE $0C, $18, $30, $30, $30, $18, $0C, $00 ' (
    DATA BYTE $30, $18, $0C, $0C, $0C, $18, $30, $00 ' )
    DATA BYTE $00, $66, $3C, $FF, $3C, $66, $00, $00 ' *
    DATA BYTE $00, $18, $18, $7E, $18, $18, $00, $00 ' +
    DATA BYTE $00, $00, $00, $00, $00, $18, $18, $30 ' ,
    DATA BYTE $00, $00, $00, $7E, $00, $00, $00, $00 ' -
    DATA BYTE $00, $00, $00, $00, $00, $18, $18, $00 ' .
    DATA BYTE $06, $0C, $18, $30, $60, $C0, $80, $00 ' /
    DATA BYTE $7C, $CE, $DE, $F6, $E6, $C6, $7C, $00 ' 0
    DATA BYTE $18, $38, $18, $18, $18, $18, $7E, $00 ' 1
    DATA BYTE $7C, $C6, $06, $7C, $C0, $C0, $FE, $00 ' 2
    DATA BYTE $FC, $06, $06, $3C, $06, $06, $FC, $00 ' 3
    DATA BYTE $0C, $CC, $CC, $CC, $FE, $0C, $0C, $00 ' 4
    DATA BYTE $FE, $C0, $FC, $06, $06, $C6, $7C, $00 ' 5
    DATA BYTE $7C, $C0, $C0, $FC, $C6, $C6, $7C, $00 ' 6
    DATA BYTE $FE, $06, $06, $0C, $18, $30, $30, $00 ' 7
    DATA BYTE $7C, $C6, $C6, $7C, $C6, $C6, $7C, $00 ' 8
    DATA BYTE $7C, $C6, $C6, $7E, $06, $06, $7C, $00 ' 9
    DATA BYTE $00, $18, $18, $00, $00, $18, $18, $00 ' :
    DATA BYTE $00, $18, $18, $00, $00, $18, $18, $30 ' ;
    DATA BYTE $0C, $18, $30, $60, $30, $18, $0C, $00 ' <
    DATA BYTE $00, $00, $7E, $00, $7E, $00, $00, $00 ' =
    DATA BYTE $30, $18, $0C, $06, $0C, $18, $30, $00 ' >
    DATA BYTE $3C, $66, $0C, $18, $18, $00, $18, $00 ' ?
    DATA BYTE $7C, $C6, $DE, $DE, $DE, $C0, $7E, $00 ' @
    DATA BYTE $38, $6C, $C6, $C6, $FE, $C6, $C6, $00 ' A
    DATA BYTE $FC, $C6, $C6, $FC, $C6, $C6, $FC, $00 ' B
    DATA BYTE $7C, $C6, $C0, $C0, $C0, $C6, $7C, $00 ' C
    DATA BYTE $F8, $CC, $C6, $C6, $C6, $CC, $F8, $00 ' D
    DATA BYTE $FE, $C0, $C0, $F8, $C0, $C0, $FE, $00 ' E
    DATA BYTE $FE, $C0, $C0, $F8, $C0, $C0, $C0, $00 ' F
    DATA BYTE $7C, $C6, $C0, $C0, $CE, $C6, $7C, $00 ' G
    DATA BYTE $C6, $C6, $C6, $FE, $C6, $C6, $C6, $00 ' H
    DATA BYTE $7E, $18, $18, $18, $18, $18, $7E, $00 ' I
    DATA BYTE $06, $06, $06, $06, $06, $C6, $7C, $00 ' J
    DATA BYTE $C6, $CC, $D8, $F0, $D8, $CC, $C6, $00 ' K
    DATA BYTE $C0, $C0, $C0, $C0, $C0, $C0, $FE, $00 ' L
    DATA BYTE $C6, $EE, $FE, $FE, $D6, $C6, $C6, $00 ' M
    DATA BYTE $C6, $E6, $F6, $DE, $CE, $C6, $C6, $00 ' N
    DATA BYTE $7C, $C6, $C6, $C6, $C6, $C6, $7C, $00 ' O
    DATA BYTE $FC, $C6, $C6, $FC, $C0, $C0, $C0, $00 ' P
    DATA BYTE $7C, $C6, $C6, $C6, $D6, $DE, $7C, $06 ' Q
    DATA BYTE $FC, $C6, $C6, $FC, $D8, $CC, $C6, $00 ' R
    DATA BYTE $7C, $C6, $C0, $7C, $06, $C6, $7C, $00 ' S
    DATA BYTE $FF, $18, $18, $18, $18, $18, $18, $00 ' T
    DATA BYTE $C6, $C6, $C6, $C6, $C6, $C6, $FE, $00 ' U
    DATA BYTE $C6, $C6, $C6, $C6, $C6, $7C, $38, $00 ' V
    DATA BYTE $C6, $C6, $C6, $C6, $D6, $FE, $6C, $00 ' W
    DATA BYTE $C6, $C6, $6C, $38, $6C, $C6, $C6, $00 ' X
    DATA BYTE $C6, $C6, $C6, $7C, $18, $30, $E0, $00 ' Y
    DATA BYTE $FE, $06, $0C, $18, $30, $60, $FE, $00 ' Z
    DATA BYTE $3C, $30, $30, $30, $30, $30, $3C, $00 ' [
    DATA BYTE $C0, $60, $30, $18, $0C, $06, $02, $00 ' \
    DATA BYTE $3C, $0C, $0C, $0C, $0C, $0C, $3C, $00 ' ]
    DATA BYTE $10, $38, $6C, $C6, $00, $00, $00, $00 ' ^
    DATA BYTE $00, $00, $00, $00, $00, $00, $00, $FF ' _
    DATA BYTE $18, $24, $24, $18, $00, $00, $00, $00 ' ` 
    DATA BYTE $00, $00, $7C, $06, $7E, $C6, $7E, $00 ' a
    DATA BYTE $C0, $C0, $C0, $FC, $C6, $C6, $FC, $00 ' b
    DATA BYTE $00, $00, $7C, $C6, $C0, $C6, $7C, $00 ' c
    DATA BYTE $06, $06, $06, $7E, $C6, $C6, $7E, $00 ' d
    DATA BYTE $00, $00, $7C, $C6, $FE, $C0, $7C, $00 ' e
    DATA BYTE $1C, $36, $30, $78, $30, $30, $78, $00 ' f
    DATA BYTE $00, $00, $7E, $C6, $C6, $7E, $06, $FC ' g
    DATA BYTE $C0, $C0, $FC, $C6, $C6, $C6, $C6, $00 ' h
    DATA BYTE $18, $00, $38, $18, $18, $18, $3C, $00 ' i
    DATA BYTE $06, $00, $06, $06, $06, $06, $C6, $7C ' j
    DATA BYTE $C0, $C0, $CC, $D8, $F8, $CC, $C6, $00 ' k
    DATA BYTE $38, $18, $18, $18, $18, $18, $3C, $00 ' l
    DATA BYTE $00, $00, $CC, $FE, $FE, $D6, $D6, $00 ' m
    DATA BYTE $00, $00, $FC, $C6, $C6, $C6, $C6, $00 ' n
    DATA BYTE $00, $00, $7C, $C6, $C6, $C6, $7C, $00 ' o
    DATA BYTE $00, $00, $FC, $C6, $C6, $FC, $C0, $C0 ' p
    DATA BYTE $00, $00, $7E, $C6, $C6, $7E, $06, $06 ' q
    DATA BYTE $00, $00, $FC, $C6, $C0, $C0, $C0, $00 ' r
    DATA BYTE $00, $00, $7E, $C0, $7C, $06, $FC, $00 ' s
    DATA BYTE $18, $18, $7E, $18, $18, $18, $0E, $00 ' t
    DATA BYTE $00, $00, $C6, $C6, $C6, $C6, $7E, $00 ' u
    DATA BYTE $00, $00, $C6, $C6, $C6, $7C, $38, $00 ' v
    DATA BYTE $00, $00, $C6, $C6, $D6, $FE, $6C, $00 ' w
    DATA BYTE $00, $00, $C6, $6C, $38, $6C, $C6, $00 ' x
    DATA BYTE $00, $00, $C6, $C6, $C6, $7E, $06, $FC ' y
    DATA BYTE $00, $00, $FE, $0C, $38, $60, $FE, $00 ' z
    DATA BYTE $3F, $60, $CF, $D8, $D8, $CF, $60, $3F
    DATA BYTE $18, $18, $18, $00, $18, $18, $18, $00 ' |
    DATA BYTE $C0, $60, $30, $30, $30, $30, $60, $C0
    DATA BYTE $76, $DC, $00, $00, $00, $00, $00, $00 ' ~
    DATA BYTE $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF '  

logoSprites: ' logo sprites for 'scanline sprites' demo
    DATA BYTE $3F, $7F, $FF, $00, $00, $FF, $FF, $FF    ' P
    DATA BYTE $E0, $E0, $E0, $00, $00, $00, $00, $00    ' 
    DATA BYTE $E0, $F8, $FC, $3C, $3C, $FC, $F8, $F0    ' 
    DATA BYTE $00, $00, $00, $00, $00, $00, $00, $00    ' 
    DATA BYTE $F0, $F0, $F0, $F0, $F0, $F0, $F0, $F0    ' I
    DATA BYTE $F0, $F0, $F0, $00, $00, $00, $00, $00    ' 
    DATA BYTE $00, $00, $00, $00, $00, $00, $00, $00    ' 
    DATA BYTE $00, $00, $00, $00, $00, $00, $00, $00    ' 
    DATA BYTE $0F, $3F, $7F, $F0, $E0, $E0, $E0, $F0    ' C
    DATA BYTE $7F, $3F, $0F, $00, $00, $00, $00, $00    ' 
    DATA BYTE $F8, $F0, $E0, $00, $00, $00, $00, $00    ' 
    DATA BYTE $F8, $F0, $E0, $00, $00, $00, $00, $00    ' 
    DATA BYTE $0F, $3F, $7F, $F0, $E0, $E0, $E0, $F0    ' O
    DATA BYTE $7F, $3F, $0F, $00, $00, $00, $00, $00    ' 
    DATA BYTE $E0, $F8, $FC, $1E, $0E, $0E, $0E, $1E    ' 
    DATA BYTE $FC, $F8, $E0, $00, $00, $00, $00, $00    ' 
    DATA BYTE $3F, $40, $8F, $90, $8F, $40, $3F, $00    ' 9
    DATA BYTE $3F, $40, $FF, $00, $00, $00, $00, $00    ' 
    DATA BYTE $F0, $08, $C4, $24, $E4, $04, $E4, $24    ' 
    DATA BYTE $C4, $08, $F0, $00, $00, $00, $00, $00    ' 
    DATA BYTE $3C, $44, $84, $E4, $24, $24, $24, $24    ' 1
    DATA BYTE $24, $24, $3C, $00, $00, $00, $00, $00    ' 
    DATA BYTE $00, $00, $00, $00, $00, $00, $00, $00    ' 
    DATA BYTE $00, $00, $00, $00, $00, $00, $00, $00    ' 
    DATA BYTE $3F, $40, $8F, $90, $4F, $40, $8F, $90    ' 8
    DATA BYTE $8F, $40, $3F, $00, $00, $00, $00, $00    ' 
    DATA BYTE $F0, $08, $C4, $24, $C8, $08, $C4, $24    ' 
    DATA BYTE $C4, $08, $F0, $00, $00, $00, $00, $00    ' 

logoSpriteWidths:
    DATA BYTE 14, 4, 13, 15, 14, 6, 14

logoSpriteIndices:  ' P, I, C, O, 9, 9, 1, 8
    DATA BYTE 0, 1, 2, 3, 4, 4,5, 6

palette: ' not currently used, but I'd prefer to use it. It stays!
    DATA BYTE $00, $00
    DATA BYTE $00, $00
    DATA BYTE $02, $C3
    DATA BYTE $05, $00
    DATA BYTE $05, $4F
    DATA BYTE $07, $6F
    DATA BYTE $0D, $54
    DATA BYTE $04, $EF
    DATA BYTE $0F, $54
    DATA BYTE $0F, $76
    DATA BYTE $0D, $C3
    DATA BYTE $0E, $D6
    DATA BYTE $02, $B2
    DATA BYTE $0C, $5C
    DATA BYTE $08, $88
    DATA BYTE $0F, $FF
  
sine: ' sine wave values for scanline sprite animation
    DATA BYTE $10, $10, $11, $11, $12, $12, $12, $13
    DATA BYTE $13, $13, $14, $14, $14, $15, $15, $15
    DATA BYTE $16, $16, $16, $16, $17, $17, $17, $17
    DATA BYTE $17, $18, $18, $18, $18, $18, $18, $18
    DATA BYTE $18, $18, $18, $18, $18, $18, $18, $18
    DATA BYTE $17, $17, $17, $17, $17, $16, $16, $16
    DATA BYTE $16, $15, $15, $15, $14, $14, $14, $13
    DATA BYTE $13, $13, $12, $12, $12, $11, $11, $10
    DATA BYTE $10, $10, $0F, $0F, $0E, $0E, $0E, $0D
    DATA BYTE $0D, $0D, $0C, $0C, $0C, $0B, $0B, $0B
    DATA BYTE $0A, $0A, $0A, $0A, $09, $09, $09, $09
    DATA BYTE $09, $08, $08, $08, $08, $08, $08, $08
    DATA BYTE $08, $08, $08, $08, $08, $08, $08, $08
    DATA BYTE $09, $09, $09, $09, $09, $0A, $0A, $0A
    DATA BYTE $0A, $0B, $0B, $0B, $0C, $0C, $0C, $0D
    DATA BYTE $0D, $0D, $0E, $0E, $0E, $0F, $0F, $10

pow2: ' 1 << INDEX
    DATA BYTE $01, $02, $04, $08, $10, $20, $40, $80

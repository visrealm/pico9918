'
' Project: pico9918
'
' Render benchmark scenes for the render-perf work.
'
' Copyright (c) 2026 Troy Schrapel
'
' This code is licensed under the MIT license
'
' https://github.com/visrealm/pico9918
'
' One ROM, eleven fixed scenes, advanced with any key or fire button. Each scene
' is static once set up - nothing animates and nothing is written to VRAM while
' it is displayed - so a diag reading is repeatable.
'
' Register writes, VRAM writes and wait-for-input only. No font, no PRINT, and
' none of CVBasic's MODE or SPRITE statements: the sprite attribute table is
' ours at $3800, so whatever the runtime does at its own default address is
' inert.
'
' The backdrop colour identifies the scene: scene N runs with backdrop N + 1.
'
' Scenes. Every T80 scene fills all 80 columns with non-blank cells; what varies
' is how often the cell colour changes, whether layer 2 is present and how it is
' distributed, and whether sprites are on screen.
'
'   0  T80 24 rows  T1      colour every 8 cells  baseline emitter
'   1  T80 24 rows  T1      colour every cell     worst case for the colour cache
'   2  T80 30 rows  T1      colour every 8 cells  cost per scanline vs per frame
'   3  T80 24 rows  T1+T2   T2 on every cell      two full emitter passes
'   4  T80 24 rows  T1+T2   T2 alternate groups   exercises the 4-cell skip
'   5  T80 24 rows  T1+T2   T2 on every cell      + 8 sprites/line, half-covering
'                                                   every 32-pixel chunk
'   6  MCM                                        multicolour, no sprites
'   7  MCM                                        + the same sprite field
'   8  GM1 unlocked  T1+T2  layer 2 scroll 7      the two layers tile each cell exactly
'   9  GM1 unlocked  T2     layer 2 scroll 7      tile layer 1 DISABLED - D4
'  10  GM1 unlocked  none   layer 2 scroll 7      both tile layers disabled - D4
'
' Scenes 9 and 10 exist for D4. With layer 1 disabled the mask still has to reach screen space,
' and nothing may be written where the mask leaves the pixel to the absent layer. Layer 2 runs at
' fine scroll 7 so a mask in the wrong space is displaced by seven of the eight pixels in a cell.
'

' ---------------------------------------------------------------------------
' VRAM map. The gaps allow 2400 bytes per table, which is 80x30.
' ---------------------------------------------------------------------------
CONST #PATT   = $0000       ' tile patterns, 2K              VR4  = 0
CONST #NAME1  = $0800       ' tile layer 1 names             VR2  = 2
CONST #COLOR1 = $1200       ' tile layer 1 attributes        VR3  = $48
CONST #NAME2  = $1C00       ' tile layer 2 names             VR10 = 7
CONST #COLOR2 = $2600       ' tile layer 2 attributes        VR11 = $98
CONST #SPATT  = $3000       ' sprite patterns                VR6  = 6
CONST #SATTR  = $3800       ' sprite attributes              VR5  = $70

CONST T80_COLS   = 80
CONST MCM_COLS   = 32
CONST GM1_COLS   = 32
CONST SPRITE_END = $D0      ' y value that terminates the sprite list
CONST SCENE_COUNT = 11

' CONT1.KEY idles at 15, not 0: it reports Coleco-style keypad codes, where
' 0-9 are the digits and 15 is "nothing pressed" (cvbasic_9900_prologue.asm:884).
' A full keyboard returns ASCII for anything else, so "not 15" means "pressed"
' on every platform this builds for.
CONST KEY_NONE = 15
DEF FN ADVANCE = (CONT1.KEY <> KEY_NONE) OR CONT.BUTTON

GOTO main

' ---------------------------------------------------------------------------
' unlock F18A registers 8-63
' ---------------------------------------------------------------------------
unlockVdp: PROCEDURE
    VDP(1) = $80            ' display off, interrupts off
    VDP(57) = $1C
    VDP(57) = $1C           ' twice, as the F18A requires
    END

' ---------------------------------------------------------------------------
' fill #fillAddr for #fillLen bytes with fillVal
' ---------------------------------------------------------------------------
fillVram: PROCEDURE
    WHILE #fillLen
        VPOKE #fillAddr, fillVal
        #fillAddr = #fillAddr + 1
        #fillLen = #fillLen - 1
    WEND
    END

' ---------------------------------------------------------------------------
' Tile patterns, four groups of 64. The shapes are what make a misalignment
' visible: two layers drawn with bars at opposite ends of their cells produce
' two interleaved column grids, and a fine-scroll difference is then countable
' in pixels rather than a matter of opinion.
'
'   names   0- 63  left half of the cell solid              layer 1
'   names  64-127  right half of the cell solid             layer 2
'   names 128-191  solid, used as a ruler mark every 8 cells
'   names 192-255  colour pairs for multicolour - both nibbles non-zero, so
'                  that mode's transparent-nibble branch behaves as it did
'                  when these scenes were first measured
'
' In an 8-pixel cell the two halves are exact complements, which is what makes a
' misaligned selection mask show up as a colour that should not be on screen at
' all - see the GM1 scenes. Only bits 7..2 reach the screen in a 6-pixel cell, so
' in text 80 they read as 4 pixels and 2 rather than 4 and 4.
' ---------------------------------------------------------------------------
loadPatterns: PROCEDURE
    #fillAddr = #PATT
    #fillLen = 512
    fillVal = $F0
    GOSUB fillVram

    #fillAddr = #PATT + 512
    #fillLen = 512
    fillVal = $0F
    GOSUB fillVram

    #fillAddr = #PATT + 1024
    #fillLen = 512
    fillVal = $FC
    GOSUB fillVram

    #p = #PATT + 1536
    v = 0
    #count = 512
    WHILE #count
        v = v + 37
        VPOKE #p, (v AND $77) OR $88
        #p = #p + 1
        #count = #count - 1
    WEND
    END

' ---------------------------------------------------------------------------
' One 16x16 sprite: left 8 columns opaque, right 8 transparent. Magnified, that
' covers 16 of the 32 pixels it spans, so a sprite on a 32-pixel boundary leaves
' its chunk half covered. Half covered is what puts the composite on its mixed
' path - fully covered would let it skip the chunk outright.
' ---------------------------------------------------------------------------
loadSpritePattern: PROCEDURE
    #fillAddr = #SPATT
    #fillLen = 16
    fillVal = $FF
    GOSUB fillVram          ' left half, rows 0-15
    #fillLen = 16
    fillVal = $00
    GOSUB fillVram          ' right half, rows 0-15
    END

' ---------------------------------------------------------------------------
' Sprite field: 4 bands of 8, on 32-pixel x boundaries. Magnified 16x16 sprites
' are 32 scanlines tall, so the bands cover the top 128 scanlines, and every
' 32-pixel chunk of those lines.
' ---------------------------------------------------------------------------
spritesOn: PROCEDURE
    #p = #SATTR
    sy = 0
    FOR band = 0 TO 3
        sx = 0
        FOR sidx = 0 TO 7
            VPOKE #p, sy
            VPOKE #p + 1, sx
            VPOKE #p + 2, 0         ' pattern 0
            VPOKE #p + 3, 15        ' opaque white, no early clock
            #p = #p + 4
            sx = sx + 32
        NEXT sidx
        sy = sy + 32
    NEXT band
    END

spritesOff: PROCEDURE
    VPOKE #SATTR, SPRITE_END
    END

' ---------------------------------------------------------------------------
' A fixed reference bar at screen x 8-15: twelve unmagnified 16x16 sprites, the
' pattern's left half being the only opaque part. Sprites never pass through the
' tile selection mask, so the bar marks an absolute screen position whatever the
' mask does - which is the only way to see a displacement in content that
' repeats every 8 pixels.
' ---------------------------------------------------------------------------
spritesMarker: PROCEDURE
    #p = #SATTR
    sy = 0
    FOR band = 0 TO 11
        VPOKE #p, sy
        VPOKE #p + 1, 8
        VPOKE #p + 2, 0         ' pattern 0
        VPOKE #p + 3, 15        ' white
        #p = #p + 4
        sy = sy + 16
    NEXT band
    VPOKE #p, SPRITE_END
    END

' ---------------------------------------------------------------------------
' Name table at #fillAddr, rows x cols. Indices walk within the shape group so
' every cell is a different pattern fetch, and none is blank.
'
'   shapeBase - 0, 64 or 192, see loadPatterns
'   ruler     - non-zero to put a solid cell every 8 columns, for counting
' ---------------------------------------------------------------------------
fillNames: PROCEDURE
    #p = #fillAddr
    v = 0
    row = rows
    WHILE row
        col = 0
        WHILE col < cols
            n = shapeBase + (v AND 63)
            IF ruler THEN
                IF (col AND 7) = 0 THEN n = 128 + (v AND 63)
            END IF
            VPOKE #p, n
            #p = #p + 1
            v = v + 1
            col = col + 1
        WEND
        row = row - 1
    WEND
    END

' ---------------------------------------------------------------------------
' Attribute table at #fillAddr for position-based attributes: fg in the high
' nibble, bg in the low one, both non-zero.
'
'   stride - cells between colour changes
'   sparse - non-zero to zero whole 4-cell groups, the only thing the layer 2
'            pass can skip
' ---------------------------------------------------------------------------
fillColors: PROCEDURE
    #p = #fillAddr
    fg = 1
    bg = 8
    left = 0
    cb = $18
    row = rows
    WHILE row
        col = 0
        WHILE col < cols
            IF left = 0 THEN
                fg = fg + 1
                IF fg > 15 THEN fg = 1
                bg = bg + 3
                IF bg > 15 THEN bg = bg - 14
                cb = fg * 16 + bg
                left = stride
            END IF
            left = left - 1
            v = cb
            IF sparse THEN
                IF (col AND 4) THEN v = 0
            END IF
            VPOKE #p, v
            #p = #p + 1
            col = col + 1
        WEND
        row = row - 1
    WEND
    END

' ---------------------------------------------------------------------------
' table bases and the scene's backdrop
' ---------------------------------------------------------------------------
setupTables: PROCEDURE
    VDP(2) = 2              ' name table 1   $0800
    VDP(3) = $48            ' attributes 1   $1200
    VDP(4) = 0              ' patterns       $0000
    VDP(5) = $70            ' sprite attrs   $3800
    VDP(6) = 6              ' sprite patts   $3000
    VDP(10) = 7             ' name table 2   $1C00
    VDP(11) = $98           ' attributes 2   $2600
    VDP(25) = 0             ' layer 2 horizontal scroll
    VDP(27) = 0             ' layer 1 horizontal scroll
    VDP(7) = $F0 + backdrop ' white on the scene's backdrop
    END

' ---------------------------------------------------------------------------
' Graphics I, unlocked. The two layers tile each cell exactly: layer 1 owns the
' left four pixels, layer 2 the right four, and layer 2 is transparent
' everywhere else. Three fixed colours, chosen so the picture answers the
' question on its own:
'
'   white      layer 1 drew here
'   black      layer 2 drew here
'   dark blue  layer 1's background, which layer 2 should cover completely
'
' So a correct scene 8 is clean four-pixel white and black stripes and NO BLUE.
' Any blue is the selection mask disagreeing with the pixels it selects between.
'
' Both layers run at fine scroll 4, and the two must be EQUAL: the halves only
' complement each other if the layers sit on the same grid. Four is chosen over
' the register's maximum because the picture repeats every 8 pixels, so a
' displacement of 7 would read as a 1-pixel nudge while 4 swaps black for white
' outright. The white sprite bar at x 8-15 is what it is measured against.
' ---------------------------------------------------------------------------
setupGm1: PROCEDURE
    rows = 24
    cols = GM1_COLS
    ruler = 0
    shapeBase = 0
    #fillAddr = #NAME1
    GOSUB fillNames
    shapeBase = 64
    #fillAddr = #NAME2
    GOSUB fillNames

    #fillAddr = #COLOR1
    #fillLen = 32
    fillVal = $F4               ' white on dark blue - opaque
    GOSUB fillVram

    #fillAddr = #COLOR2
    #fillLen = 32
    fillVal = $10               ' black on transparent
    GOSUB fillVram
    END

' ---------------------------------------------------------------------------
' scene dispatch
' ---------------------------------------------------------------------------
setupScene: PROCEDURE
    VDP(1) = $80            ' display off while VRAM is rewritten
    backdrop = scene + 1
    GOSUB setupTables

    IF scene < 6 THEN
        ' ---- text 80 ----
        rows = 24
        IF scene = 2 THEN rows = 30
        cols = T80_COLS

        stride = 8
        IF scene = 1 THEN stride = 1

        shapeBase = 0
        ruler = 1
        #fillAddr = #NAME1
        GOSUB fillNames
        sparse = 0
        #fillAddr = #COLOR1
        GOSUB fillColors

        r49 = 0
        IF scene >= 3 THEN
            shapeBase = 64
            ruler = 0
            #fillAddr = #NAME2
            GOSUB fillNames
            sparse = 0
            IF scene = 4 THEN sparse = 1
            #fillAddr = #COLOR2
            GOSUB fillColors
            r49 = $80                   ' tile layer 2 on
        END IF
        IF rows = 30 THEN r49 = r49 + $40

        IF scene = 5 THEN
            GOSUB spritesOn
        ELSE
            GOSUB spritesOff
        END IF

        VDP(0) = $04                    ' text 80
        VDP(49) = r49
        VDP(50) = $02                   ' position-based attributes
        VDP(1) = $F3                    ' display, int, text, 16x16 magnified
    ELSEIF scene < 8 THEN
        ' ---- multicolour ----
        rows = 24
        cols = MCM_COLS
        shapeBase = 192
        ruler = 0
        #fillAddr = #NAME1
        GOSUB fillNames

        IF scene = 7 THEN
            GOSUB spritesOn
        ELSE
            GOSUB spritesOff
        END IF

        VDP(0) = $00
        VDP(49) = $00
        VDP(50) = $00
        VDP(1) = $EB                    ' display, int, multicolour, 16x16 mag
    ELSE
        ' ---- graphics I, unlocked ----
        GOSUB setupGm1
        GOSUB spritesMarker

        r49 = $80                       ' tile layer 2 on
        r50 = $00
        IF scene = 9 THEN r50 = $10                 ' layer 1 disabled
        IF scene = 10 THEN
            r49 = $00                               ' both layers disabled
            r50 = $10
        END IF

        VDP(0) = $00
        VDP(25) = 4                     ' layer 2 fine scroll
        VDP(27) = 4                     ' layer 1 fine scroll - the SAME as layer 2
        VDP(49) = r49
        VDP(50) = r50
        VDP(1) = $E2                    ' display, int, 16x16 unmagnified
    END IF
    END

' ---------------------------------------------------------------------------
' any key or fire button, on a press edge
' ---------------------------------------------------------------------------
waitButton: PROCEDURE
    WHILE ADVANCE           ' release first, so one press is one scene
        WAIT
    WEND
    WHILE 1
        WAIT
        IF ADVANCE THEN EXIT WHILE
    WEND
    END

' ---------------------------------------------------------------------------
main:
    GOSUB unlockVdp
    GOSUB loadPatterns
    GOSUB loadSpritePattern

    scene = 0
    WHILE 1
        GOSUB setupScene
        GOSUB waitButton
        scene = scene + 1
        IF scene = SCENE_COUNT THEN scene = 0
    WEND

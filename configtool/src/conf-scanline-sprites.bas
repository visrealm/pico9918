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

spriteIndices:
DATA BYTE 0,1,2,3,4,4,5,6
' -----------------------------------------------------------------------------
' initialise the sprite attributes
' -----------------------------------------------------------------------------
initSprites: PROCEDURE
    CONST NUM_SPRITES = 16
    DIM spriteAttr(NUM_SPRITES * 4)

    xPos = 16

    FOR I = 0 TO NUM_SPRITES - 1
        spritePattIndex = spriteIndices(I AND 7)
        spriteAttr(I * 4 + 0) = $d0
        spriteAttr(I * 4 + 1) = xPos
        spriteAttr(I * 4 + 2) = spritePattIndex * 4
        spriteAttr(I * 4 + 3) = 15
        xPos = xPos + logoSpriteWidths(spritePattIndex) + 1
        IF (I AND 7) = 7 THEN xPos = xPos + 8  ' small gap
    NEXT I

    END

' -----------------------------------------------------------------------------
' animate the sprites for 'scanline sprites' option
' -----------------------------------------------------------------------------
animateSprites: PROCEDURE

    CONST spritePosY = 127

    ' "static" values
    s_startAnimIndex = s_startAnimIndex + 3

    ' update all y positions
    FOR I = 0 TO NUM_SPRITES - 1
        spriteAttrIdx = I * 4
        spriteAttr(spriteAttrIdx) = spritePosY + sine((s_startAnimIndex + spriteAttr(spriteAttrIdx + 1)) AND $7f)
    NEXT I

    s_startSpriteIndex = s_startSpriteIndex + 1
    if s_startSpriteIndex >= NUM_SPRITES THEN s_startSpriteIndex = 0

    WAIT

    ' dump it to vram (sprite attribute table)
    DEFINE VRAM #VDP_SPRITE_ATTR, (NUM_SPRITES - s_startSpriteIndex) * 4, VARPTR spriteAttr(s_startSpriteIndex * 4)
    IF s_startSpriteIndex > 0 THEN
        DEFINE VRAM #VDP_SPRITE_ATTR + (NUM_SPRITES - s_startSpriteIndex) * 4, s_startSpriteIndex * 4, VARPTR spriteAttr(0)
    END IF

    SPRITE NUM_SPRITES, $d0, 0,0,0

    END    

' -----------------------------------------------------------------------------
' hide the sprites when 'scanline sprites' option no longer selected
' -----------------------------------------------------------------------------
hideSprites: PROCEDURE
    FOR I = 0 TO 1
        SPRITE I,$d0,0,0,0
    NEXT I
    END

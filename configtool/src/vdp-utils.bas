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

' VDP constants
CONST #VDP_NAME_TAB      = $1800
CONST #VDP_SPRITE_ATTR   = $1B00
CONST #VDP_FIRMWARE_DATA = $1D00
CONST #VDP_SPRITE_PATT   = $3800


CONST #VDP_PATT_TAB1     = $0000
CONST #VDP_PATT_TAB2     = #VDP_PATT_TAB1 + $0800
CONST #VDP_PATT_TAB3     = #VDP_PATT_TAB2 + $0800

CONST #VDP_COLOR_TAB1    = $2000
CONST #VDP_COLOR_TAB2    = #VDP_COLOR_TAB1 + $0800
CONST #VDP_COLOR_TAB3    = #VDP_COLOR_TAB2 + $0800


#if TMS9918_TESTING
    DEF FN VDP_REG(VR) = IF (VR < 8) THEN VDP(VR)
    DEF FN VDP_STATUS = 0
#else
    DEF FN VDP_REG(VR) = VDP(VR)
    DEF FN VDP_STATUS = USR RDVST
#endif

DEF FN VDP_CONFIG(I) = VDP_REG(58) = I : VDP_REG(59) ' = xxx
DEF FN VDP_STATUS_REG = VDP_REG(15)
DEF FN VDP_STATUS_REG0 = VDP_STATUS_REG = 0

' -----------------------------------------------------------------------------
' Palette port (VDP register 47).
'
' The configurator runs with TWO palette pages active at once:
'
'   Page 0 (PAL_TARGET) - the user's saved configuration. This is what the
'                         host machine sees through the VDP. Edits to the
'                         configurable colors land here. Entry 0 is the
'                         hardware background colour and must not be
'                         clobbered by preset writes.
'   Page 1 (PAL_UI)     - the configurator's chrome palette, populated from
'                         defPal (TMS9918A defaults). The UI tile patterns
'                         are drawn against this page, so entries 0..15 of
'                         this page must not change once setupTiles has run,
'                         except for entry 1 (PAL_UI_PREVIEW), which is
'                         repurposed as the "currently edited color" preview
'                         shown next to the RGB sliders.
'
' Register 47 layout:
'   bit 7 = enable, bit 6 = autoincrement, bit 4 = page select,
'   bits 3..0 = entry index. So $c0 + 16*page + entry opens the port,
'   and $40 closes it (autoincrement only, write disabled).
' -----------------------------------------------------------------------------
CONST PAL_PORT_OPEN     = $c0
CONST PAL_PORT_CLOSE    = $40
CONST PAL_PAGE_TARGET   = 0
CONST PAL_PAGE_UI       = 1
CONST PAL_UI_PREVIEW    = 1     ' page-1 entry used as live edit preview

DEF FN PAL_PORT(PAGE, ENTRY) = VDP_REG(47) = PAL_PORT_OPEN + (PAGE) * 16 + (ENTRY)
DEF FN PAL_PORT_END         = VDP_REG(47) = PAL_PORT_CLOSE

' VDP helpers
DEF FN VDP_DISABLE_INT = VDP_REG(1) = $C2
DEF FN VDP_ENABLE_INT = VDP_REG(1) = $E2
DEF FN VDP_DISABLE_INT_DISP_OFF = VDP_REG(1) = $82
DEF FN VDP_ENABLE_INT_DISP_OFF = VDP_REG(1) = $A2
' name table helpers
DEF FN XY(X, Y) = ((Y) * 32 + (X))                      ' PRINT AT XY(1, 2), ...

DEF FN NAME_TAB_XY(X, Y) = (#VDP_NAME_TAB + XY(X, Y))   ' DEFINE VRAM NAME_TAB_XY(1, 2), ...
DEF FN PUT_XY(X, Y) = VPOKE NAME_TAB_XY(X, Y)     ' place a byte in the name table
DEF FN GET_XY(X, Y) = VPEEK(NAME_TAB_XY(X, Y))          ' read a byte from the name table


' -----------------------------------------------------------------------------
' detect the vdp type. sets isF18ACompatible
' -----------------------------------------------------------------------------
vdpDetect: PROCEDURE
    GOSUB vdpUnlock
    DEFINE VRAM $3F00, 6, vdpGpuDetect
    VDP_REG($36) = $3F                       ' set gpu start address msb
    VDP_REG($37) = $00                       ' set gpu start address lsb (triggers)
    isF18ACompatible = VPEEK($3F00) = 0  ' check result
    isV9938 = FALSE
    IF isF18ACompatible = FALSE THEN
        VDP_STATUS_REG = 4
        isV9938 = ((VDP_STATUS AND $fe) = $fe)
        VDP_STATUS_REG0
    END IF
    END
    
' -----------------------------------------------------------------------------
' unlock F18A mode
' -----------------------------------------------------------------------------
vdpUnlock: PROCEDURE
    VDP_DISABLE_INT_DISP_OFF
    VDP_REG(57) = $1C                       ' unlock
    VDP_REG(57) = $1C                       ' unlock... again
    VDP_ENABLE_INT_DISP_OFF
    END

' -----------------------------------------------------------------------------
' TMS9900 machine code (for PICO9918 GPU) to write $00 to VDP $3F00
' -----------------------------------------------------------------------------
vdpGpuDetect:
    DATA BYTE $04, $E0    ' CLR  @>3F00
    DATA BYTE $3F, $00
    DATA BYTE $03, $40    ' IDLE    

defaultReg: ' default VDP register values
    DATA BYTE $02, $82, $06, $FF, $03, $36, $07, $00
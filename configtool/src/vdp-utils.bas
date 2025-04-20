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

' VDP constants
CONST #VDP_NAME_TAB      = $1800
CONST #VDP_SPRITE_ATTR   = $1B00
CONST #VDP_FIRMWARE_DATA = $1D00

CONST #VDP_PATT_TAB1     = $0000
CONST #VDP_PATT_TAB2     = $0800
CONST #VDP_PATT_TAB3     = $1000

CONST #VDP_COLOR_TAB1    = $2000
CONST #VDP_COLOR_TAB2    = $2800
CONST #VDP_COLOR_TAB3    = $3000

' VDP helpers
DEF FN VDP_DISABLE_INT = VDP(1) = $C2
DEF FN VDP_ENABLE_INT = VDP(1) = $E2
DEF FN VDP_DISABLE_INT_DISP_OFF = VDP(1) = $82
DEF FN VDP_ENABLE_INT_DISP_OFF = VDP(1) = $A2
DEF FN VDP_WRITE_CONFIG(I, V) = VDP(58) = I : VDP(59) = V
DEF FN VDP_READ_STATUS = USR RDVST
DEF FN VDP_SET_CURRENT_STATUS_REG(R) = VDP(15) = R
DEF FN VDP_RESET_STATUS_REG = VDP_SET_CURRENT_STATUS_REG(0)

' name table helpers
DEF FN XY(X, Y) = ((Y) * 32 + (X))                      ' PRINT AT XY(1, 2), ...

DEF FN NAME_TAB_XY(X, Y) = (#VDP_NAME_TAB + XY(X, Y))   ' DEFINE VRAM NAME_TAB_XY(1, 2), ...
DEF FN PUT_XY(X, Y, C) = VPOKE NAME_TAB_XY(X, Y), C     ' place a byte in the name table
DEF FN GET_XY(X, Y) = VPEEK(NAME_TAB_XY(X, Y))          ' read a byte from the name table


' -----------------------------------------------------------------------------
' detect the vdp type. sets isF18ACompatible
' -----------------------------------------------------------------------------
vdpDetect: PROCEDURE
    GOSUB vdpUnlock
    DEFINE VRAM $3F00, 6, vdpGpuDetect
    VDP($36) = $3F                       ' set gpu start address msb
    VDP($37) = $00                       ' set gpu start address lsb (triggers)
    isF18ACompatible = VPEEK($3F00) = 0  ' check result
    END
    
' -----------------------------------------------------------------------------
' unlock F18A mode
' -----------------------------------------------------------------------------
vdpUnlock: PROCEDURE
    VDP_DISABLE_INT_DISP_OFF
    VDP(57) = $1C                       ' unlock
    VDP(57) = $1C                       ' unlock... again
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
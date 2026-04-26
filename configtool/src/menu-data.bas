'
' Project: pico9918
'
' PICO9918 Configurator
'
' This code is licensed under the MIT license
'
' https://github.com/visrealm/pico9918
'

' -----------------------------------------------------------------------------
' Menu offsets and counts into configMenuData.
' -----------------------------------------------------------------------------
CONST MENU_COUNT_MAIN       = 9
CONST MENU_DATA_COUNT_MAIN  = MENU_COUNT_MAIN + 1
CONST MENU_COUNT_POPUP      = 2
CONST MENU_COUNT_INFO       = 1
CONST MENU_COUNT_DIAG       = 5
CONST MENU_COUNT_PALETTE    = 2

CONST MENU_OFFSET_MAIN      = 0
CONST MENU_OFFSET_POPUP     = MENU_OFFSET_MAIN  + MENU_DATA_COUNT_MAIN
CONST MENU_OFFSET_INFO      = MENU_OFFSET_POPUP + MENU_COUNT_POPUP
CONST MENU_OFFSET_DIAG      = MENU_OFFSET_INFO  + MENU_COUNT_INFO
CONST MENU_OFFSET_PALETTE   = MENU_OFFSET_DIAG  + MENU_COUNT_DIAG

' -----------------------------------------------------------------------------
' Option-value offsets and counts into configMenuOptionValueData.
' -----------------------------------------------------------------------------
CONST OPT_COUNT_ONOFF       = 2
CONST OPT_COUNT_SPRITES     = 4
CONST OPT_COUNT_CLOCK       = 3
CONST OPT_COUNT_PALETTE     = 5

CONST OPT_OFFSET_ONOFF      = 0
CONST OPT_OFFSET_SPRITES    = OPT_OFFSET_ONOFF   + OPT_COUNT_ONOFF
CONST OPT_OFFSET_CLOCK      = OPT_OFFSET_SPRITES + OPT_COUNT_SPRITES
CONST OPT_OFFSET_PALETTE    = OPT_OFFSET_CLOCK   + OPT_COUNT_CLOCK

' -----------------------------------------------------------------------------
' Pico9918Options index, name[16], values index, num values, help[32]
' -----------------------------------------------------------------------------
configMenuData:
    ' Main menu - MENU_OFFSET_MAIN, MENU_COUNT_MAIN (+1 trailing empty row)
    DATA BYTE CONF_CRT_SCANLINES,    "CRT scanlines   ", OPT_OFFSET_ONOFF,   OPT_COUNT_ONOFF,   "    Faux CRT scanline effect    "
    DATA BYTE CONF_SCANLINE_SPRITES, "Scanline sprites", OPT_OFFSET_SPRITES, OPT_COUNT_SPRITES, "                                "
    DATA BYTE CONF_CLOCK_PRESET_ID,  "Clock frequency ", OPT_OFFSET_CLOCK,   OPT_COUNT_CLOCK,   "  MCU clock  (requires reboot)  "
    DATA BYTE CONF_MENU_DIAG,        "Diagnostics  >>>", 0,                  0,                 "   Manage diagnostics options   "
    DATA BYTE CONF_MENU_PALETTE,     "Palette      >>>", 0,                  0,                 "     Change default palette     "
    DATA BYTE CONF_MENU_INFO,        "Device info. >>>", 0,                  0,                 "    View device information     "
#if BANK_SIZE
    DATA BYTE CONF_MENU_FIRMWARE,    "Firmware     >>>", 0,                  0,                 "        Update firmware         "
#endif
    DATA BYTE CONF_MENU_RESET,       "Reset defaults  ", 0,                  0,                 " Reset to default configuration "
    DATA BYTE CONF_MENU_SAVE,        "Save settings   ", 0,                  0,                 " Save configuration to PICO9918 "
    DATA BYTE CONF_MENU_EMPTY,       "                ", 0,                  0,                 "                                "
#if NOT BANK_SIZE
    DATA BYTE CONF_MENU_EMPTY,       "                ", 0,                  0,                 "                                "
#endif

    ' Confirmation popup (Save / Update firmware) - MENU_OFFSET_POPUP, MENU_COUNT_POPUP
    DATA BYTE CONF_MENU_OK,          "Confirm         ", 0,                  0,                 " Save configuration to PICO9918 "
    DATA BYTE CONF_MENU_CANCEL,      "Cancel          ", 0,                  0,                 "        Back to main menu       "

    ' Device Info "<<< Main menu" row - MENU_OFFSET_INFO, MENU_COUNT_INFO
    DATA BYTE CONF_MENU_CANCEL,      "<<< Main menu   ", 0,                  0,                 "        Back to main menu       "

    ' Diagnostics submenu - MENU_OFFSET_DIAG, MENU_COUNT_DIAG
    DATA BYTE CONF_DIAG_REGISTERS,   "Registers       ", OPT_OFFSET_ONOFF,   OPT_COUNT_ONOFF,   "      Show VDP registers        "
    DATA BYTE CONF_DIAG_PERFORMANCE, "Performance     ", OPT_OFFSET_ONOFF,   OPT_COUNT_ONOFF,   "     Show performance data      "
    DATA BYTE CONF_DIAG_ADDRESS,     "Addresses       ", OPT_OFFSET_ONOFF,   OPT_COUNT_ONOFF,   "      Show VRAM addresses       "
    DATA BYTE CONF_DIAG_PALETTE,     "Palette         ", OPT_OFFSET_ONOFF,   OPT_COUNT_ONOFF,   "         Show palettes          "
    DATA BYTE CONF_MENU_CANCEL,      "<<< Main menu   ", 0,                  0,                 "        Back to main menu       "

    ' Palette submenu - MENU_OFFSET_PALETTE, MENU_COUNT_PALETTE
    DATA BYTE CONF_MENU_RESET,       "Preset          ", OPT_OFFSET_PALETTE, OPT_COUNT_PALETTE, "      Select preset palette     "
    DATA BYTE CONF_MENU_CANCEL,      "<<< Main menu   ", 0,                  0,                 "        Back to main menu       "

' -----------------------------------------------------------------------------
' Pico9918Option values. Indexed from options()
' -----------------------------------------------------------------------------
configMenuOptionValueData:
    ' On/Off - OPT_OFFSET_ONOFF, OPT_COUNT_ONOFF
    DATA BYTE "Off   "
    DATA BYTE "On    "

    ' Scanline sprite counts - OPT_OFFSET_SPRITES, OPT_COUNT_SPRITES
    DATA BYTE "4     "
    DATA BYTE "8     "
    DATA BYTE "16    "
    DATA BYTE "32    "

    ' Clock frequencies - OPT_OFFSET_CLOCK, OPT_COUNT_CLOCK
    DATA BYTE "252MHz"
    DATA BYTE "302MHz"
    DATA BYTE "352MHz"

    ' Palette presets - OPT_OFFSET_PALETTE, OPT_COUNT_PALETTE
    DATA BYTE "9918A "
    DATA BYTE "V9938 "
    DATA BYTE "GREY  "
    DATA BYTE "SEPIA "
    DATA BYTE "EGA   "

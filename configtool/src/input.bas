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

CONST NAV_NONE = 0
CONST NAV_DOWN = 1
CONST NAV_UP = 2
CONST NAV_LEFT = 4
CONST NAV_RIGHT = 8
CONST NAV_OK = 16
CONST NAV_CANCEL = 32

' -----------------------------------------------------------------------------
' centralised navigation handling for kb and joystick
' -----------------------------------------------------------------------------
updateNavInput: PROCEDURE
    g_nav = NAV_NONE
    g_key = CONT1.key

    IF g_key >= 48 AND g_key <= 57 THEN 
        g_key = g_key - 48
    ELSEIF g_key >= 65 AND g_key <= 90 THEN 
        g_key = g_key - 55
    ELSEIF g_key > 9 THEN
        g_key = 0
    END IF

    ' <DOWN> or <X>
    IF CONT.DOWN OR (CONT1.KEY = "X") THEN g_nav = g_nav OR NAV_DOWN

    ' <UP> or <E>
    IF CONT.UP OR (CONT1.KEY = "E") THEN g_nav = g_nav OR NAV_UP

    ' <RIGHT> or <D> or (<.> [>])
    IF CONT.RIGHT OR (CONT1.KEY = "D") OR (CONT1.KEY = ".") THEN g_nav = g_nav OR NAV_RIGHT

    ' <LEFT> or <S> or (<,> [<])
    IF CONT.LEFT OR (CONT1.KEY = "S") OR (CONT1.KEY = ",") THEN g_nav = g_nav OR NAV_LEFT

    ' <LBUTTON> or <SPACE> OR <ENTER>
    IF CONT.BUTTON2 OR (CONT1.KEY = " ") OR (CONT1.KEY = 11) THEN g_nav = g_nav OR NAV_OK

    ' <RBUTTON> or <Q> OR <ESC>
    IF CONT.BUTTON OR (CONT1.KEY = "Q") OR (CONT1.KEY = 27) THEN g_nav = g_nav OR NAV_CANCEL
    END


waitForInput: PROCEDURE
    WHILE 1
        WAIT

        GOSUB updateNavInput

        IF (g_nav) THEN EXIT WHILE
    WEND
    END
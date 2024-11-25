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
' centralised navigation handling for kb and joystick
' -----------------------------------------------------------------------------
getNavButton: PROCEDURE
    g_nav = NAV_NONE

    ' <DOWN> or <X>
    IF CONT.DOWN OR (CONT1.KEY = "X") THEN g_nav = g_nav OR NAV_DOWN

    ' <UP> or <E>
    IF CONT.UP OR (CONT1.KEY = "E") THEN g_nav = g_nav OR NAV_UP

    ' <RIGHT> or <D> or (<.> [>])
    IF CONT.RIGHT OR (CONT1.KEY = "D") OR (CONT1.KEY = ".") THEN g_nav = g_nav OR NAV_RIGHT

    ' <LEFT> or <S> or (<,> [<])
    IF CONT.LEFT OR (CONT1.KEY = "S") OR (CONT1.KEY = ",") THEN g_nav = g_nav OR NAV_LEFT

    ' <LBUTTON> or <SPACE> OR <ENTER>
    IF CONT.BUTTON OR (CONT1.KEY = " ") OR (CONT1.KEY = 11) THEN g_nav = g_nav OR NAV_OK

    ' <RBUTTON> or <Q> OR <ESC>
    IF CONT.BUTTON2 OR (CONT1.KEY = "Q") OR (CONT1.KEY = 27) THEN g_nav = g_nav OR NAV_CANCEL
    END

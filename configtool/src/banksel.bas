GOTO ENDBANKSEL

#if BANK_SIZE

g_currentBank = 0

    DEF FN BANKSEL(BANK) = g_currentBank = BANK : GOSUB selectBank

selectBank0:
    BANK SELECT 0
    RETURN

selectBank1:
    BANK SELECT 1
    RETURN

selectBank2:
    BANK SELECT 2
    RETURN

selectBank3:
    BANK SELECT 3
    RETURN

selectBank4:
    BANK SELECT 4
    RETURN

selectBank5:
    BANK SELECT 5
    RETURN

selectBank6:
    BANK SELECT 6
    RETURN

selectBank7:
    BANK SELECT 7
    RETURN

selectBank8:
    BANK SELECT 8
    RETURN

selectBank9:
    BANK SELECT 9
    RETURN

selectBank10:
    BANK SELECT 10
    RETURN

selectBank11:
    BANK SELECT 11
    RETURN

selectBank12:
    BANK SELECT 12
    RETURN

selectBank:
    ON g_currentBank FAST GOSUB selectBank0,selectBank1,selectBank2,selectBank3,selectBank4,selectBank5,selectBank6,selectBank7,selectBank8,selectBank9,selectBank10,selectBank11,selectBank12
    RETURN
#else
    DEF FN BANKSEL(BANK) = B = BANK
#endif

ENDBANKSEL:
#if BANK_SIZE

g_currentBank = 0

    DEF FN BANKSEL(BANK) = g_currentBank = BANK : GOSUB selectBank

selectBank0: PROCEDURE
    BANK SELECT 0
    END

selectBank1: PROCEDURE
    BANK SELECT 1
    END

selectBank2: PROCEDURE
    BANK SELECT 2
    END

selectBank3: PROCEDURE
    BANK SELECT 3
    END

selectBank4: PROCEDURE
    BANK SELECT 4
    END

selectBank5: PROCEDURE
    BANK SELECT 5
    END

selectBank6: PROCEDURE
    BANK SELECT 6
    END

selectBank7: PROCEDURE
    BANK SELECT 7
    END

selectBank8: PROCEDURE
    BANK SELECT 8
    END

selectBank9: PROCEDURE
    BANK SELECT 9
    END

selectBank10: PROCEDURE
    BANK SELECT 10
    END

selectBank11: PROCEDURE
    BANK SELECT 11
    END

selectBank12: PROCEDURE
    BANK SELECT 12
    END

selectBank: PROCEDURE
    ON g_currentBank FAST GOSUB selectBank0,selectBank1,selectBank2,selectBank3,selectBank4,selectBank5,selectBank6,selectBank7,selectBank8,selectBank9,selectBank10,selectBank11,selectBank12
    END

#else   ' No banking
    DEF FN BANKSEL(BANK) = B = BANK
#endif
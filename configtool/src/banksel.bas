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

selectBank13: PROCEDURE
    BANK SELECT 13
    END

selectBank14: PROCEDURE
    BANK SELECT 14
    END

selectBank15: PROCEDURE
    BANK SELECT 15
    END

selectBank: PROCEDURE
    ON g_currentBank FAST GOSUB selectBank0,selectBank1,selectBank2,selectBank3,selectBank4,selectBank5,selectBank6,selectBank7,selectBank8,selectBank9,selectBank10,selectBank11,selectBank12,selectBank13,selectBank14,selectBank15
    END

#else   ' No banking
    DEF FN BANKSEL(BANK) = B = BANK
#endif

#if TI994A

selectBank16: PROCEDURE
    BANK SELECT 16
    END

selectBank17: PROCEDURE
    BANK SELECT 17
    END

selectBank18: PROCEDURE
    BANK SELECT 18
    END

selectBank19: PROCEDURE
    BANK SELECT 19
    END

selectBank20: PROCEDURE
    BANK SELECT 20
    END

selectBank21: PROCEDURE
    BANK SELECT 21
    END

selectBank22: PROCEDURE
    BANK SELECT 22
    END

selectBank23: PROCEDURE
    BANK SELECT 23
    END

selectBank24: PROCEDURE
    BANK SELECT 24
    END

selectBank25: PROCEDURE
    BANK SELECT 25
    END

selectBank26: PROCEDURE
    BANK SELECT 26
    END

selectBank27: PROCEDURE
    BANK SELECT 27
    END

selectBank28: PROCEDURE
    BANK SELECT 28
    END

selectBank29: PROCEDURE
    BANK SELECT 29
    END

selectBank30: PROCEDURE
    BANK SELECT 30
    END

selectBank31: PROCEDURE
    BANK SELECT 31
    END

selectBank: PROCEDURE
    ON g_currentBank FAST GOSUB selectBank0,selectBank1,selectBank2,selectBank3,selectBank4,selectBank5,selectBank6,selectBank7,selectBank8,selectBank9,selectBank10,selectBank11,selectBank12,selectBank13,selectBank14,selectBank15,selectBank16,selectBank17,selectBank18,selectBank19,selectBank20,selectBank21,selectBank22,selectBank23,selectBank24,selectBank25,selectBank26,selectBank27,selectBank28,selectBank29,selectBank30,selectBank31
    END

#endif

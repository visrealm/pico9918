SLAST

;;; CV BASIC Epilogue

; data in low RAM
    dorg >2000

; must be even aligned
; mirror for sprite table
sprites	    bss 128

; Vars can start at >2080
    dorg >2080


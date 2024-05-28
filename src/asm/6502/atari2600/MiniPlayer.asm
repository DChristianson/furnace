;
; Minimal audio player - 
;

    processor 6502
    include "vcs.h"
    include "macro.h"

NTSC = 0
PAL60 = 1

    IFNCONST SYSTEM
SYSTEM = NTSC
    ENDIF

; ----------------------------------
; constants

#if SYSTEM = NTSC
; NTSC Colors
BLUE = $A0
RED = $30
WHITE = $0f
BLACK = 0
SCANLINES = 262
#else
; PAL Colors
BLUE = $90
RED = $40
WHITE = $0E
BLACK = 0
SCANLINES = 262
#endif

GRADIENT_FIELD_HEIGHT = 193
OVERSCAN_HEIGHT = 31

; ----------------------------------
; variables

  SEG.U variables

    ORG $80

    AUDIO_VARS

; ----------------------------------
; code

  SEG
    ORG $F000

Reset
CleanStart
    ; do the clean start macro
            CLEAN_START

            ; load track
            jsr audio_play_track

newFrame

    ; 3 scanlines of vertical sync signal to follow

            ldx #%00000010
            stx VSYNC               ; turn ON VSYNC bit 1
            ldx #0

            sta WSYNC               ; wait a scanline
            sta WSYNC               ; another
            sta WSYNC               ; another = 3 lines total

            stx VSYNC               ; turn OFF VSYNC bit 1

    ; 34 scanlines of vertical blank to follow

;--------------------
; VBlank start

            lda #%00000010
            sta VBLANK

            lda #42    ; vblank timer will land us ~ on scanline 34
            sta TIM64T

    ; check reset switches
            lda #$01
            bit SWCHB
            bne _end_switches
            jmp CleanStart
_end_switches

;---------------------
; audio tracker

            jsr audio_update

;---------------------
; end vblank

waitOnVBlank
            ldx #$00
waitOnVBlank_loop          
            cpx INTIM
            bmi waitOnVBlank_loop
            stx VBLANK
            sta WSYNC ; SL 38
            
            ldx #GRADIENT_FIELD_HEIGHT
gradient_loop
            sta WSYNC
            stx COLUBK
            dex
            bne gradient_loop
            stx COLUBK

;--------------------
; Footer + Overscan 

            ldx #OVERSCAN_HEIGHT
overscan_loop
            sta WSYNC
            dex
            bne overscan_loop
            jmp newFrame

;-----------------------------------------------------------------------------------
; Code

  #include "Player_core.asm"

;-----------------------------------------------------------------------------------
; Audio

  #include "Track_data.asm"

;-----------------------------------------------------------------------------------
; the CPU reset vectors

    ORG $FFFA

    .word Reset          ; NMI
    .word Reset          ; RESET
    .word Reset          ; IRQ

    END
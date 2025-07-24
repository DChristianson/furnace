    MAC AUDIO_VARS
    IF AUDIO_TRACK_ADDRESS_BITS <= 8
audio_track         ds 1  ; what track are we on
audio_channel             ; index to next action in each channel
audio_channel_0     ds 1  ;
audio_channel_1     ds 1  ;
audio_timer               ; time left before next action in each channel
audio_timer_0       ds 1  ; 
audio_timer_1       ds 1  ; 
    ELSE
audio_track         ds 1  ; what track are we on
audio_channel             ; index to next action in each channel
audio_channel_0     ds 2  ;
audio_channel_1     ds 2  ;
audio_timer               ; time left before next action in each channel
audio_timer_0       ds 1  ; 
audio_timer_1       ds 1  ; 
    ENDIF
    ENDM

    IFNCONST audio_cx
audio_cx = AUDC0
audio_fx = AUDF0 
audio_vx = AUDV0
    ENDIF

    MAC AUDIO_CONTROLS
audio_inc_track
            ldy audio_track
            iny
            cpy #AUDIO_NUM_TRACKS
            bne _audio_track_save
            ldy #0
            beq _audio_track_save ; always true but cheaper than jump
audio_dec_track
            ldy audio_track
            dey
            bpl _audio_track_save
            ldy #(AUDIO_NUM_TRACKS - 1)
_audio_track_save
            sty audio_track
            rts
    IF AUDIO_TRACK_ADDRESS_BITS <= 8
audio_play_track
            ldy audio_track
            lda AUDIO_TRACKS_0,y
            sta audio_channel_0
            lda AUDIO_TRACKS_1,y
            sta audio_channel_1
            lda #0
            sta audio_timer_0
            sta audio_timer_1
            rts
    ELSE
audio_play_track
            ldy audio_track
            lda AUDIO_TRACKS_0_LO,y
            sta audio_channel_0
            lda AUDIO_TRACKS_0_HI,y
            sta audio_channel_0+1
            lda AUDIO_TRACKS_1_LO,y
            sta audio_channel_1
            lda AUDIO_TRACKS_1_HI,y
            sta audio_channel_1+1
            lda #0
            sta audio_timer_0
            sta audio_timer_1
            rts
    ENDIF
    ENDM

    MAC AUDIO_UPDATE
    IF AUDIO_TRACK_ADDRESS_BITS <= 8
audio_update
            ldx #1 ; loop over both audio channels
_audio_loop
            ldy audio_timer,x
            beq _audio_next_note
            dey
            sty audio_timer,x
            jmp _audio_next_channel
_audio_next_note
            ldy audio_channel,x 
            lda AUDIO_DATA-1,y
            beq _audio_next_channel    ; check for zero 
            lsr                        ; .......|C pull first bit
            bcc _set_all_registers     ; .......|? if clear go to load all registers
            lsr                        ; 0......|C1 pull second bit
            bcc _set_cx_vx             ; 0......|?1 if clear we are loading aud(c|v)x
            lsr                        ; 00fffff|C11 pull duration bit for later set
            sta audio_fx,x             ; store frequency
            bpl _set_timer_delta       ; jump to duration (note: should always be positive)
_set_cx_vx  lsr                        ; 00.....|C01
            bcc _set_vx                ; 00.....|?01  
            lsr                        ; 000cccc|C101
            sta audio_cx,x             ; store control
            bpl _set_timer_delta       ; jump to duration (note: should always be positive)
_set_vx
            lsr                        ; 000vvvv|C001
            sta audio_vx,x             ; store volume
_set_timer_delta
            rol audio_timer,x          ; set new timer to 0 or 1 depending on carry bit
            bpl _audio_advance_note    ; done (note: should always be positive)
_set_all_registers
            ; processing all registers
            lsr                        ; 00......|C0
            bcc _set_suspause          ; 00......|?0 if clear we are suspausing
            lsr                        ; 0000fffff|C10 pull duration bit
            sta audio_fx,x             ; store frequency
            rol audio_timer,x          ; set new timer to 0 or 1 depending on carry bit
            iny                        ; advance 1 byte
            lda AUDIO_DATA-1,y         ; ccccvvvv|
            sta audio_vx,x             ; store volume
            lsr                        ; 0ccccvvv|
            lsr                        ; 00ccccvv|
            lsr                        ; 000ccccv|
            lsr                        ; 0000cccc|
            sta audio_cx,x             ; store control
            bpl _audio_advance_note    ; done (note: should always be positive)
_set_suspause
            lsr                        ; 000ddddd|C00 pull bit 3 (reserved)
            sta audio_timer,x          ; store timer
            bcs _audio_advance_note    ; if set we sustain
            lda #0
            sta audio_vx,x             ; clear volume
_audio_advance_note
            iny
            sty audio_channel,x
_audio_next_channel
            dex
            bpl _audio_loop
            rts

    ELSE

audio_update
            ldx #2 ; loop over both audio channels
            ldy #1
_audio_loop
            lda audio_timer,y
            beq _audio_next_note
            sec
            sbc #1
            sta audio_timer,y
            jmp _audio_next_channel
_audio_next_note
            lda (audio_channel,x)
            beq _audio_next_channel    ; check for zero 
            lsr                        ; .......|C pull first bit
            bcc _set_all_registers     ; .......|? if clear go to load all registers
            lsr                        ; 0......|C1 pull second bit
            bcc _set_cx_vx             ; 0......|?1 if clear we are loading aud(c|v)x
            lsr                        ; 00fffff|C11 pull duration bit for later set
            sta audio_fx,y             ; store frequency
            bpl _set_timer_delta       ; jump to duration (note: should always be positive)
_set_cx_vx  lsr                        ; 00.....|C01
            bcc _set_vx                ; 00.....|?01  
            lsr                        ; 000cccc|C101
            sta audio_cx,y             ; store control
            bpl _set_timer_delta       ; jump to duration (note: should always be positive)
_set_vx
            lsr                        ; 000vvvv|C001
            sta audio_vx,y             ; store volume
_set_timer_delta
            lda #0
            rol
            sta audio_timer,y          ; set new timer to 0 or 1 depending on carry bit
            bpl _audio_advance_note    ; done (note: should always be positive)
_set_all_registers
            ; processing all registers
            lsr                        ; 00......|C0
            bcc _set_suspause          ; 00......|?0 if clear we are suspausing
            lsr                        ; 0000fffff|C10 pull duration bit
            sta audio_fx,y             ; store frequency
            lda #0
            rol
            sta audio_timer,y          ; set new timer to 0 or 1 depending on carry bit
            jsr audio_channel_advance  ; advance 1 byte
            lda (audio_channel,x)      ; ccccvvvv|
            sta audio_vx,y             ; store volume
            lsr                        ; 0ccccvvv|
            lsr                        ; 00ccccvv|
            lsr                        ; 000ccccv|
            lsr                        ; 0000cccc|
            sta audio_cx,y             ; store control
            bpl _audio_advance_note    ; done (note: should always be positive)
_set_suspause
            lsr                        ; 000ddddd|C00 pull bit 3 (reserved)
            sta audio_timer,y          ; store timer
            bcs _audio_advance_note    ; if set we sustain
            lda #0
            sta audio_vx,y             ; clear volume
_audio_advance_note
            jsr audio_channel_advance
_audio_next_channel
            dex
            dex
            dey
            bpl _audio_loop
            rts

audio_channel_advance
            lda audio_channel,x
            clc
            adc #1
            sta audio_channel,x
            lda #0
            adc audio_channel+1,x
            sta audio_channel+1,x
            rts

    ENDIF
    ENDM
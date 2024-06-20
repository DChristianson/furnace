    MAC AUDIO_VARS
audio_data_ptr             ; audio register data
audio_data_0_ptr    ds 2   ; channel 0
audio_data_1_ptr    ds 2   ; channel 1
audio_track_ptr            ; audio track data
audio_track_0_ptr   ds 2   ; channel 0
audio_track_1_ptr   ds 2   ; channel 1
audio_timer 
audio_timer_0       ds 1   ; 
audio_decode_0      ds 1
audio_timer_1       ds 1   ; 
audio_decode_1      ds 1
audio_track         ds 1
    ENDM

    IFNCONST audio_cx
audio_cx = AUDC0
audio_fx = AUDF0 
audio_vx = AUDV0
    ENDIF

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

audio_play_track
            lda audio_track
            asl
            asl
            asl
            tay
            ldx #7
_audio_play_setup_loop
            lda AUDIO_TRACKS,y
            sta audio_data_ptr,x
            iny
            dex
            bpl _audio_play_setup_loop
            lda #0
            sta audio_timer_0
            sta audio_timer_1
            rts

audio_update
            ldx #2 ; loop over both audio channels
            ldy #1
_audio_loop
            lda audio_timer,x
            beq _audio_next_note
            dec audio_timer,x
            jmp _audio_next_channel
_audio_next_note
            lda (audio_data_ptr,x)
            beq _audio_pop             ; check for zero 
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
            rol audio_timer,x          ; set new timer to 0 or 1 depending on carry bit
            bpl _audio_advance_note    ; done (note: should always be positive)
_set_all_registers
            ; processing all registers
            lsr                        ; 00......|C0
            bcc _set_suspause          ; 00......|?0 if clear we are suspausing
            lsr                        ; 0000fffff|C10 pull duration bit
            sta audio_fx,y             ; store frequency
            rol audio_timer,x          ; set new timer to 0 or 1 depending on carry bit
            jsr audio_data_advance
            lda (audio_data_ptr,x)     ; ccccvvvv|
            sta audio_vx,y            ; store volume
            lsr                        ; 0ccccvvv|
            lsr                        ; 00ccccvv|
            lsr                        ; 000ccccv|
            lsr                        ; 0000cccc|
            sta audio_cx,y             ; store control
            bpl _audio_advance_note    ; done (note: should always be positive)
_set_suspause
            lsr                        ; 000.....|C00 
            bcc _audio_goto            ; 000.....|?00 if clear we are doing a goto
            lsr                        ; 0000dddd|C000 if set we are sustaining
            sta audio_timer,x          ;
            bcs _audio_advance_note
            lda #0
            sta audio_vx,y             ; clear volume
_audio_advance_note
            jsr audio_data_advance
_audio_next_channel
            ldx #0
            dey
            bpl _audio_loop
            rts
_audio_pop
            ; pull an address from jump stream
            lda (audio_track_ptr,x)
            beq audio_play_track ; restart song
            sta audio_data_ptr+1,x
            jsr audio_track_advance
            lda (audio_track_ptr,x)
            sta audio_data_ptr,x
            jsr audio_track_advance
            jmp _audio_next_note

_audio_goto 
            ; pull an address from data stream
            pha
            jsr audio_data_advance
            lda (audio_data_ptr,x) 
            sta audio_data_ptr,x
            pla
            sta audio_data_ptr+1,x
            jmp _audio_next_note

audio_data_advance
            clc
            lda audio_data_ptr,x
            adc #1
            sta audio_data_ptr,x
            lda #0
            adc audio_data_ptr+1,x
            sta audio_data_ptr+1,x
            rts

audio_track_advance
            clc
            lda audio_track_ptr,x
            adc #1
            sta audio_track_ptr,x
            lda #0
            adc audio_track_ptr+1,x
            sta audio_track_ptr+1,x
            rts            
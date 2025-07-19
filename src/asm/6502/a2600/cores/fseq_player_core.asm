    MAC AUDIO_VARS
audio_song          ds 1  ; what song are we on
audio_song_ptr      ds 2  ; address of song
audio_song_order    ds 1  ; what order are we at in the song
audio_row_idx       ds 1  ; where are we in the current order
audio_pattern_idx   ds 2  ; which pattern is playing on each channel
audio_pattern_ptr   ds 2
audio_waveform_idx  ds 2  ; where are we in waveform on each channel
audio_waveform_ptr  ds 2
audio_timer         ds 2  ; time left on next action on each channel
    ENDM

    IFNCONST audio_cx
audio_cx = AUDC0
audio_fx = AUDF0 
audio_vx = AUDV0
    ENDIF

audio_inc_track
            ldy audio_song
            iny
            cpy #NUM_SONGS
            bne _song_save
            ldy #0
            jmp _song_save
audio_dec_track
            ldy audio_song
            dey
            bpl _song_save
            ldy #(NUM_SONGS - 1)
_song_save
            sty audio_song

audio_play_track
            ldy audio_song
            lda SONG_TABLE_START_LO,y
            sta audio_song_ptr
            lda SONG_TABLE_START_HI,y
            sta audio_song_ptr + 1
            ldy #0
            ldx #(audio_timer + 1 - audio_row_idx)
_song_clean_loop
            sty audio_row_idx,x
            dex
            bpl _song_clean_loop
            lda (audio_song_ptr),y
            sta audio_pattern_idx
            iny
            lda (audio_song_ptr),y
            sta audio_pattern_idx+1
            iny
            sty audio_song_order
            rts

audio_update
            ldx #1 ; loop over both audio channels
_audio_loop
            ldy audio_timer,x
            beq _audio_next_note
            dey
            sty audio_timer,x
            jmp _audio_next_channel
_audio_next_note
            ldy audio_pattern_idx,x 
            lda PAT_TABLE_START_LO,y
            sta audio_pattern_ptr
            lda PAT_TABLE_START_HI,y
            sta audio_pattern_ptr + 1
            ldy audio_row_idx
            lda (audio_pattern_ptr),y
            tay                       ; y is now waveform ptr
            lda WF_TABLE_START_LO,y
            sta audio_waveform_ptr
            lda WF_TABLE_START_HI,y
            sta audio_waveform_ptr + 1
            ldy audio_waveform_idx,x
            lda (audio_waveform_ptr),y
            beq _audio_advance_tracker ; check for zero 
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
            lda (audio_waveform_ptr),y ; ccccvvvv|
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
            sty audio_waveform_idx,x
            jmp _audio_next_channel
_audio_advance_tracker ; got a 0 on waveform
            lda #255
            sta audio_timer,x
            sta audio_waveform_idx,x
_audio_next_channel
            dex
            bpl _audio_loop

            ; update track - check if both waveforms done
            lda audio_waveform_idx
            and audio_waveform_idx+1
            cmp #255
            bne _audio_end            
            lda #0
            sta audio_timer
            sta audio_timer+1
            sta audio_waveform_idx
            sta audio_waveform_idx+1
            ldy audio_row_idx
            iny
            lda (audio_pattern_ptr),y
            cmp #255
            beq _audio_advance_order
            sty audio_row_idx
            jmp audio_update; if not 255 loop back 
_audio_advance_order ; got a 255 on pattern
            lda #0
            sta audio_row_idx
            ldy audio_song_order
            lda (audio_song_ptr),y
            cmp #255
            bne _audio_advance_order_advance_pattern
            ldy #0
            lda (audio_song_ptr),y
_audio_advance_order_advance_pattern
            sta audio_pattern_idx
            iny
            lda (audio_song_ptr),y
            sta audio_pattern_idx+1
            iny
            sty audio_song_order
            jmp audio_update;  loop back 
_audio_end
            rts


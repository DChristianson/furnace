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

    MAC AUDIO_LDA_VIS_PAT
            ldy audio_row_idx
            lda (audio_pattern_ptr),y
    ENDM

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
            rts

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
            lsr                        ; pull first bit
            bcc _set_registers         ; if clear go to load registers
            lsr                        ; check second bit
            bcc _set_cx_vx             ; if clear we are loading aud(c|v)x
            lsr                        ; pull duration bit for later set
            sta audio_fx,x             ; store frequency
            jmp _set_timer_delta       ; jump to duration 
_set_cx_vx  lsr
            bcc _set_vx ; BUGBUG broken
            lsr
            sta audio_cx,x
            jmp _set_timer_delta       ; jump to duration
_set_vx
            lsr
            sta audio_vx,x
_set_timer_delta
            lda #0
            adc #0                     ; will be 0 or 1 based on carry bit
            sta audio_timer,x
            jmp _audio_advance_waveform
_set_registers
            ; processing all registers
            pha                        ; save timer
            lsr
            lsr
            sta audio_fx,x
            iny
            pla                      
            and #$03
            lsr
            bcs _set_timer_registers
            lda (audio_waveform_ptr),y
            iny
_set_timer_registers
            sta audio_timer,x
            lda (audio_waveform_ptr),y
            lsr
            lsr
            lsr
            lsr
            sta audio_cx,x
            lda (audio_waveform_ptr),y
            and #$0f
            sta audio_vx,x
_audio_advance_waveform
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


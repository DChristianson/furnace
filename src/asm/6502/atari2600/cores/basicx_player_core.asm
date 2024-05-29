    MAC AUDIO_VARS
audio_track         ds 1  ;
audio_channel             ; index to next action in each channel
audio_channel_0     ds 1  ;
audio_channel_1     ds 1  ;
audio_timer               ; time left before next action in each channel
audio_timer_0       ds 1  ; 
audio_timer_1       ds 1  ; 
    ENDM

    IFNCONST audio_cx
audio_cx = AUDC0
audio_fx = AUDF0 
audio_vx = AUDV0
    ENDIF

    MAC AUDIO_LDA_VIS_PAT
            ldy audio_channel
            lda AUDIO_F,y
            adc AUDIO_CV,y
    ENDM            

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
            beq _audio_next_channel
            lda AUDIO_F-1,y
            sta audio_fx,x
            lsr
            lsr
            lsr
            lsr
            lsr
            sta audio_timer,x
            lda AUDIO_CV-1,y
            sta audio_vx,x
            beq _audio_incr
            lsr
            lsr
            lsr
            lsr
            sta audio_cx,x
            iny
            tya
_audio_incr
            sta audio_channel,x
_audio_next_channel
            dex
            bpl _audio_loop
_audio_end
            rts

    MAC AUDIO_VARS

audio_stream_ptr
audio_data_0_ptr        ds 2   ; channel 0
audio_data_1_ptr        ds 2   ; channel 1
audio_track_0_ptr       ds 2   ; channel 0
audio_track_1_ptr       ds 2   ; channel 1
audio_jump_0_ptr        ds 2   ; channel 0
audio_jump_1_ptr        ds 2   ; channel 1

audio_data_last_addr           
audio_data_0_last_addr  ds 2
audio_data_1_last_addr  ds 2
audio_data_max_addr
audio_data_0_max_addr   ds 2
audio_data_1_max_addr   ds 2

audio_data_0_shift      ds 1
audio_track_0_shift     ds 1
audio_data_1_shift      ds 1
audio_track_1_shift     ds 1

audio_timer 
audio_timer_0       ds 1   ; 
audio_timer_1       ds 1   ; 
audio_track         ds 1
    ENDM

    IFNCONST audio_cx
audio_cx = AUDC0
audio_fx = AUDF0 
audio_vx ds 2
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
            ; BUGBUG: will be off
            sta audio_data_ptr,x
            sta audio_last_ptr,x
            sta audio_front_ptr,x
            iny
            dex
            bpl _audio_play_setup_loop
            lda #0
            sta audio_timer_0
            sta audio_timer_1
            sta audio_data_0_bits
            sta audio_data_1_bits
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
            ... do all the things
_audio_next_channel
            ldx #0
            dey
            bpl _audio_loop
            rts

_audio_track_jump
            ; pull an address from jump stream
            ...
_audio_track_return_last
            ...
_audio_track_return_front
            ...

_audio_skip
            ....

_audio_goto 
            
            inc audio_stream_ptr,x
            bne _audio_jump_same_page
            inc audio_stream_ptr+1,x
_audio_jump_same_page
            lda (audio_stream_ptr,x)

            clc
_load_first_byte            ror
            bne _load_first_byte
            lda audio_data_shift,x

            ; pull an address from data stream
            pha
            jsr audio_data_advance
            lda (audio_data_ptr,x) 
            sta audio_data_ptr,x
            pla
            sta audio_data_ptr+1,x
            jmp _audio_next_note

            ; read one data bit from audio stream
            ; uses a sentinel bit and a few tricks picked up from
            ; http://forum.6502.org/viewtopic.php?f=2&t=4642    
audio_read_bit
            lsr audio_stream_shift,x
            bne _audio_read_data_bit_end
            inc audio_stream_ptr,x
            bne _audio_read_bit_same_page
            inc audio_stream_ptr+1,x
_audio_read_bit_same_page
            lda (audio_stream_ptr,x)
            ror
            sta audio_stream_shift,x
_audio_read_bit_end
            rts

            ; jump to a location on the data stream
            ; address coords on stack
            ;  hhhhhlll lllllsss - h = high bits, l = low bits, s = shift
audio_jump_address
            ; BUGBUG: way to efficiently save last and front
            pla
            sta audio_stream_ptr+1,x
            pla
            sta audio_stream_ptr,x
            ; 3x shift down
            lsr audio_stream_ptr+1,x
            lsr audio_stream_ptr,x
            lsr audio_stream_ptr+1,x
            lsr audio_stream_ptr,x
            lsr audio_stream_ptr+1,x
            lsr audio_stream_ptr,x
            ; setup shift register
            and #$07
            tay
            lda (audio_stream_ptr,x)
            dey
            bmi _audio_shift_back
            sec
_audio_jump_shift
            lsr
            dey
            bne _audio_jump_shift
            sta audio_data_shift,x
            rts
_audio_shift_back
            lda audio_stream_ptr,x
            beq _audio_shift_back_same_page
            dec audio_stream_ptr+1,x
_audio_shift_back_same_page
            dec audio_stream_ptr,x
            lda #1
            sta audio_data_bits,x
            rts

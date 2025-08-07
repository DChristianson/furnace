    MAC AUDIO_VARS

audio_track         ds 1

audio_stream_ptr
audio_stream_lo
audio_stream_hi = . + 1
audio_data_0_ptr   ds 2 ; channel 0 data stream
audio_data_1_ptr   ds 2 ; channel 1 data stream
audio_span_0_ptr   ds 2 ; channel 0 span stream
audio_span_1_ptr   ds 2 ; channel 1 span stream

audio_timer
audio_timer_0      ds 1
audio_timer_1      ds 1

audio_stream_buf  
audio_data_0_buf   ds 1 ; channel 0 data stream
first_code         ds 1
audio_data_1_buf   ds 1 ; channel 1 data stream
first_idx          ds 1
audio_span_0_buf   ds 1 ; channel 0 span stream
curr_code_len      ds 1
audio_span_1_buf   ds 1 ; channel 1 span stream
command_ptr
command_ptr_lo
symbol             ds 1
command_ptr_hi     ds 1


audio_data_last_ptr        ; position where we took last jump in data stream
audio_data_0_last_ptr   ds 2 ; 
audio_data_1_last_ptr   ds 2 ; 
audio_data_max_ptr         ; furthest read position in data stream
audio_data_0_max_ptr    ds 2 ; 
audio_data_1_max_ptr   ds 2 ;

    ENDM

    IFNCONST audio_cx
audio_cx = AUDC0
audio_fx = AUDF0 
audio_vx ds 2
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
audio_play_track
            lda audio_track
            asl
            asl
            asl
            tay
            ldx #7
_audio_play_setup_loop
            lda AUDIO_TRACKS,y
            sta audio_stream_ptr,x
            iny
            dex
            bpl _audio_play_setup_loop
            ldx #3
_audio_play_pointer_copy
            lda audio_stream_ptr,x
            sta audio_data_last_ptr,x
            sta audio_data_max_ptr,x
            dex
            bpl _audio_play_pointer_copy
            lda #0
            sta audio_timer_0
            sta audio_timer_1
            sta audio_data_0_buf
            sta audio_data_1_buf
            lda #>CODE_WRITE_REGISTERS_111
            sta command_ptr_hi
            rts
    ENDM

    MAC AUDIO_UPDATE

; --- Bit reading ---
audio_update
            ldx #1
_audio_update_loopback:
            lda audio_timer,x
            beq _audio_update_command
            dec audio_timer,x
            bpl _audio_update_next_channel
_audio_update_command
            audio_decode_command_MACRO
            sta command_ptr_lo
            jmp (command_ptr)
CODE_WRITE_REGISTERS_111:
            audio_decode_control_MACRO
            sta AUDC0,x
CODE_WRITE_REGISTERS_011:
            jsr audio_decode_frequency
            audio_decode_volume_MACRO
            sta AUDV0,x
            bcc _audio_update_next_channel
CODE_WRITE_REGISTERS_010:
            jsr audio_decode_frequency
            bcc _audio_update_next_channel
CODE_PAUSE:
            lda #0
            sta AUDV0,x
CODE_SUSTAIN:
            audio_decode_duration_MACRO
            sta audio_timer,x
            bcc _audio_update_next_channel
CODE_BRANCH_POINT:
            ;BUGBUG: something
            ;jsr InitStreams
_audio_update_next_channel
            dex
            bpl _audio_update_loopback
            rts

audio_decode_frequency
            audio_decode_frequency_MACRO
            sta AUDF0,x
            rts


; --- Canonical Huffman Decoder ---

audio_stream_read_symbol:
            sta curr_code_len
            sty first_idx 
            lda #0
            sta symbol
            sta first_code
            txa
            pha
            asl
            tax
            ; read one data bit from audio stream
            ; uses a sentinel bit and a few tricks picked up from
            ; http://forum.6502.org/viewtopic.php?f=2&t=4642    
_audio_read_next_bit
            lsr audio_stream_buf,x
            bne _audio_read_bit_end
            inc audio_stream_lo,x
            bne _audio_read_bit_same_page
            inc audio_stream_hi,x
_audio_read_bit_same_page
            lda (audio_stream_ptr,x)
            sec ; set sentinel bit
            ror
            sta audio_stream_buf,x
_audio_read_bit_end
            rol symbol
            inc curr_code_len
            ldy curr_code_len
            lda symbol 
            sec
            sbc CODEBOOK_LENGTHS,y
            bcc _return_symbol
            cmp first_code
            bcc _return_symbol

            lda CODEBOOK_LENGTHS,y ; first_code = (first_code + count[curr_len]) << 1
            clc
            adc first_code
            asl
            sta first_code
            
            lda CODEBOOK_LENGTHS,y ; first_idx = first_idx + count[curr_len]
            clc
            adc first_idx
            sta first_idx
            jmp _audio_read_next_bit
_return_symbol:
            lda symbol         ; huffman coded symbol
            sec
            sbc first_code      ; offset = curr_code - first_code
            clc
            adc first_idx      ; symbol index = offset + symbol_off
            tay
            pla
            tax
            lda CODEBOOK_CODES,y
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
            sta audio_stream_buf,x
            rts
_audio_shift_back
            lda audio_stream_ptr,x
            beq _audio_shift_back_same_page
            dec audio_stream_ptr+1,x
_audio_shift_back_same_page
            dec audio_stream_ptr,x
            lda #1
            sta audio_stream_buf,x
            rts
    ENDM
    MAC AUDIO_VARS

audio_track           ds 1

audio_channel_idx     ds 1
audio_stream_idx      ds 1

audio_stream_ptr
audio_stream_lo
audio_stream_hi = . + 1
audio_data_0_ptr      ds 2 ; channel 0 data stream
audio_data_1_ptr      ds 2 ; channel 1 data stream
audio_span_0_ptr      ds 2 ; channel 0 span stream
audio_span_1_ptr      ds 2 ; channel 1 span stream

audio_timer
audio_timer_0         ds 1
audio_timer_1         ds 1

audio_stream_buf  
audio_data_0_buf      ds 1 ; channel 0 data stream
audio_stream_next_addr_lo
first_code            ds 1
audio_data_1_buf      ds 1 ; channel 1 data stream
audio_stream_next_addr_hi
first_idx             ds 1
audio_span_0_buf      ds 1 ; channel 0 span stream
audio_stream_next_addr_buf
curr_code_len         ds 1 ; BUGBUG: unused
audio_span_1_buf      ds 1 ; channel 1 span stream
command_ptr
command_ptr_lo
symbol                ds 1
command_ptr_hi        ds 1

audio_data_last_ptr
audio_data_last_lo
audio_data_last_hi = . + 1
audio_data_0_last_ptr ds 2 ; position where we took last jump in data stream
audio_data_1_last_ptr ds 2 ; 
audio_data_ff_ptr
audio_data_ff_lo
audio_data_ff_hi = . + 1
audio_data_0_ff_ptr   ds 2 ; max position where we took last jump in data stream
audio_data_1_ff_ptr   ds 2 ; 
audio_data_last_buf
audio_data_0_last_buf ds 1 ; last jump buffer state
audio_data_ff_buf
audio_data_0_ff_buf   ds 1
audio_data_1_last_buf ds 1
audio_data_1_ff_buf   ds 1

audio_channel_vx      ds 2

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
            sta audio_data_ff_ptr,x
            dex
            bpl _audio_play_pointer_copy
            lda #0
            sta audio_timer_0
            sta audio_timer_1
            sta audio_data_0_buf
            sta audio_data_1_buf
            sta audio_vx
            sta audio_vx+1
            lda #>CODE_WRITE_REGISTERS_111
            sta command_ptr_hi
            rts
    ENDM

    MAC AUDIO_UPDATE

ADDRESS_INDEX_BITS = 5
ADDRESS_BITS_HI = 4
ADDRESS_BITS_LO = 8
ADDRESS_BITS_BUF = 3
ADDRESS_BITS = 15

; --- Bit reading ---
audio_update
            ldx #1
_audio_update_loopback:
            stx audio_channel_idx
            txa
            asl
            sta audio_stream_idx
            lda audio_timer,x
            beq _audio_update_next_command
            dec audio_timer,x
            bpl _audio_update_next_channel
_audio_update_next_command
            audio_decode_command_MACRO
            sta command_ptr_lo
            jmp (command_ptr)
CODE_WRITE_REGISTERS_111:
            audio_decode_control_MACRO
            sta audio_cx,x
CODE_WRITE_REGISTERS_011:
            jsr audio_decode_frequency
CODE_WRITE_REGISTERS_001:
            audio_decode_volume_MACRO
            sta audio_channel_vx,x
            byte $2c
CODE_VOL_INC:
            inc audio_channel_vx,x
            byte $2c
CODE_VOL_DEC:
            dec audio_channel_vx,x
            jmp _audio_update_next_channel
CODE_WRITE_REGISTERS_010:
            jsr audio_decode_frequency
            jmp _audio_update_next_channel ; BUGBUG space?
CODE_PAUSE:
            lda #0
            sta audio_channel_vx,x
CODE_SUSTAIN:
            audio_decode_duration_MACRO
            sta audio_timer,x
            bcc _audio_update_next_channel
CODE_BRANCH_POINT:
            lda SPAN_IDX,x
            sta audio_stream_idx
            audio_decode_span_MACRO
            sta command_ptr_lo
            txa
            asl
            sta audio_stream_idx
            jmp (command_ptr)
CODE_STOP = audio_play_track
CODE_SKIP:
            jsr audio_stream_skip_address
            jmp _audio_update_next_command 
_audio_update_next_channel
            lda audio_channel_vx,x
            sta audio_vx,x
            dex
            bpl _audio_update_loopback
            rts
CODE_RETURN_FF:
            ldx audio_stream_idx
            lda audio_data_ff_lo,x
            sta audio_stream_lo,x
            lda audio_data_ff_hi,x
            sta audio_stream_hi,x
            lda audio_data_ff_buf,x
            sta audio_stream_buf,x
            jmp _audio_skip_shift ; BUGBUG true?
CODE_RETURN_LAST:
            ldx audio_stream_idx
            lda audio_data_last_lo,x
            sta audio_stream_lo,x
            lda audio_data_last_hi,x
            sta audio_stream_hi,x
            lda audio_data_last_buf,x
            sta audio_stream_buf,x
            jmp _audio_skip_shift ; BUGBUG true?
CODE_TAKE_TRACK_JUMP:
            jsr audio_stream_skip_address
            jsr audio_stream_save_context
            ldx audio_channel_idx
            lda SPAN_IDX,x
            sta audio_stream_idx
            jsr audio_stream_read_address
            ldx audio_channel_idx
            txa
            asl
            sta audio_stream_idx
            jmp _audio_seek
CODE_TAKE_DATA_JUMP:
            jsr audio_stream_read_address
            jsr audio_stream_save_context
_audio_seek
            ldy audio_stream_next_addr_buf
            lda audio_stream_next_addr_lo
            clc
            adc #<(AUDIO_DATA_OFFSET-1)
            sta audio_stream_lo,x
            lda audio_stream_next_addr_hi
            adc #>(AUDIO_DATA_OFFSET-1)
            sta audio_stream_hi,x
_audio_do_shift
            lda #0
            sta audio_stream_buf,x
            dey
            bmi _audio_skip_shift
            jsr audio_stream_read_bits
_audio_skip_shift
            ldx audio_channel_idx
            jmp _audio_update_next_command            

SPAN_IDX
  byte 4,6

audio_decode_frequency
            audio_decode_frequency_MACRO
            sta audio_fx,x
            cmp #$20
            bmi _audio_decode_frequency_lead
            lda #12
            sta audio_cx,x
_audio_decode_frequency_lead
            rts

; --- Canonical Huffman Decoder ---

audio_stream_read_symbol:
            sty first_idx 
            tay
            lda #0
            sta symbol
            sta first_code
            ldx audio_stream_idx
_symbol_read_next_bit
            READ_BIT
            rol symbol
            iny
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

            jmp _symbol_read_next_bit
_return_symbol:
            lda symbol         ; huffman coded symbol
            sec
            sbc first_code      ; offset = curr_code - first_code
            clc
            adc first_idx      ; symbol index = offset + symbol_off
            tay
            lda CODEBOOK_CODES,y
            ldx audio_channel_idx
            rts

audio_stream_save_context
            ldx audio_stream_idx
            lda audio_stream_buf,x
            sta audio_data_last_buf,x
            lda audio_stream_lo,x
            sta audio_data_last_lo,x
            lda audio_stream_hi,x
            sta audio_data_last_hi,x
            cmp audio_data_ff_hi,x
            bne _audio_stream_save_ff
            lda audio_data_last_lo,x
            cmp audio_data_ff_lo,x
            bne _audio_stream_save_ff
            lda audio_data_ff_buf,x
            cmp audio_data_last_buf,x
_audio_stream_save_ff
            bcc _audio_stream_save_return
            lda audio_data_last_buf,x
            sta audio_data_ff_buf,x
            lda audio_data_last_lo,x
            sta audio_data_ff_lo,x
            lda audio_data_last_hi,x
            sta audio_data_ff_hi,x
_audio_stream_save_return
            rts

audio_stream_read_address
            ldx audio_stream_idx
            ; jump to a location on the data stream
            ; 15 bits of address coords on stack
            ;  hhhhlll lllllsss - h = high bits, l = low bits, s = shift
            READ_BIT
            bcs _audio_stream_read_in_stream
            ldy #(ADDRESS_INDEX_BITS - 1)
            jsr audio_stream_read_bits
            tay
            lda AUDIO_JUMP_TABLE_LO_START,y
            sta audio_stream_next_addr_lo
            lda AUDIO_JUMP_TABLE_HI_START,y
            and #$0f
            sta audio_stream_next_addr_hi
            lda AUDIO_JUMP_TABLE_HI_START,y
            lsr
            lsr
            lsr
            lsr
            bpl _audio_stream_read_return
_audio_stream_read_in_stream
            ldy #(ADDRESS_BITS_HI - 1)
            jsr audio_stream_read_bits
            sta audio_stream_next_addr_hi
            ldy #(ADDRESS_BITS_LO - 1)
            jsr audio_stream_read_bits
            sta audio_stream_next_addr_lo
            ldy #(ADDRESS_BITS_BUF - 1)
            jsr audio_stream_read_bits
_audio_stream_read_return
            sta audio_stream_next_addr_buf
            rts

audio_stream_skip_address
            ldx audio_stream_idx
            READ_BIT
            ldy #(ADDRESS_INDEX_BITS - 1)
            bcc _audio_skip_bits
            ldy #(ADDRESS_BITS - 1)
_audio_skip_bits
            jsr audio_stream_read_bits
            rts

audio_stream_read_bits
            lda #0
            sta symbol
            ldx audio_stream_idx
_read_bits_loop
            READ_BIT
            rol symbol
            dey
            bpl _read_bits_loop
            lda symbol
            rts

    ENDM

    MAC READ_BIT
            ; read one data bit from audio stream
            ; uses a sentinel bit and a few tricks picked up from
            ; http://forum.6502.org/viewtopic.php?f=2&t=4642    
            lsr audio_stream_buf,x
            bne ._audio_read_bit_end
            inc audio_stream_lo,x
            bne ._audio_read_bit_same_page
            inc audio_stream_hi,x
._audio_read_bit_same_page
            lda (audio_stream_ptr,x)
            sec ; set sentinel bit
            ror
            sta audio_stream_buf,x
._audio_read_bit_end
    ENDM

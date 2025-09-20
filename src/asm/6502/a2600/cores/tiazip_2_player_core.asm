    MAC AUDIO_VARS

audio_track           ds 1

audio_channel_idx     ds 1
audio_data_stream_idx ds 1
audio_span_stream_idx ds 1

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

audio_channel_cx      ds 2
audio_channel_dv      ds 2
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
            sta audio_data_stream_idx
            lda SPAN_IDX,x
            sta audio_span_stream_idx
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
            sta audio_channel_cx,x
CODE_WRITE_REGISTERS_011:
            jsr audio_decode_frequency
CODE_WRITE_REGISTERS_001:
            audio_decode_volume_MACRO
            bmi CODE_VELOCITY
            sta audio_channel_vx,x
            lda #0
CODE_VELOCITY
            sta audio_channel_dv,x
            clc
            adc audio_channel_vx,x
            and #0x0f
            sta audio_channel_vx,x
            jmp _audio_update_next_channel
CODE_WRITE_REGISTERS_010:
            jsr audio_decode_frequency
            jmp _audio_update_vx ; BUGBUG space?
CODE_SUSTAIN:
            audio_decode_duration_MACRO
            sta audio_timer,x
_audio_update_vx
            lda audio_channel_dv,x
            jmp CODE_VELOCITY
CODE_BRANCH_POINT:
            audio_decode_span_MACRO
            sta command_ptr_lo
            jmp (command_ptr)
CODE_STOP = audio_play_track
CODE_SKIP:
            jsr audio_data_stream_skip_address
            jmp _audio_update_next_command 
_audio_update_next_channel
            lda audio_channel_vx,x
            sta audio_vx,x
            dex
            bpl _audio_update_loopback
            rts
CODE_RETURN_FF:
            ldx audio_data_stream_idx
            lda audio_data_ff_lo,x
            sta audio_stream_lo,x
            lda audio_data_ff_hi,x
            sta audio_stream_hi,x
            lda audio_data_ff_buf,x
            jmp _audio_end_shift_loop ; BUGBUG true?
CODE_RETURN_LAST:
            ldx audio_data_stream_idx
            lda audio_data_last_lo,x
            sta audio_stream_lo,x
            lda audio_data_last_hi,x
            sta audio_stream_hi,x
            lda audio_data_last_buf,x
            jmp _audio_end_shift_loop ; BUGBUG true?
CODE_TAKE_TRACK_JUMP:
            jsr audio_data_stream_skip_address
            ldx audio_span_stream_idx
            byte $2c ; skip next 2 bytes
CODE_TAKE_DATA_JUMP:
            ldx audio_data_stream_idx
            ; jump to a location on the data stream
            ; 15 bits of address coords on stack
            ;  hhhhlll lllllsss - h = high bits, l = low bits, s = shift
            lda audio_stream_buf,x
            READ_BIT_NO_SAVE_BUF
            bcs _audio_stream_read_in_stream
            ldy #%11110000
            sty symbol
_audio_stream_read_jump_idx
            READ_BIT_NO_SAVE_BUF
            rol symbol
            bcs _audio_stream_read_jump_idx
            sta audio_stream_buf,x
            ldy symbol
            lda AUDIO_JUMP_TABLE_LO_START,y
            sta audio_stream_next_addr_lo
            lda AUDIO_JUMP_TABLE_HI_START,y
            ora #$f0
            sta audio_stream_next_addr_hi
            lda AUDIO_JUMP_TABLE_HI_START,y
            lsr
            lsr
            lsr
            lsr
            sta audio_stream_next_addr_buf
            bpl _audio_stream_read_return
_audio_stream_read_in_stream
            READ_BIT_NO_SAVE_BUF
            bcs _audio_stream_read_long
            ldy #%00000000
            sty audio_stream_next_addr_hi
            bcc _audio_stream_read_short
_audio_stream_read_long
            ldy #%11101111
            sty audio_stream_next_addr_hi
_audio_stream_read_hi
            READ_BIT_NO_SAVE_BUF
            rol audio_stream_next_addr_hi
            bcs _audio_stream_read_hi
_audio_stream_read_short
            ldy #%11111110
            sty audio_stream_next_addr_lo
_audio_stream_read_lo
            READ_BIT_NO_SAVE_BUF
            rol audio_stream_next_addr_lo
            bcs _audio_stream_read_lo
            ldy #%11000000
            sty audio_stream_next_addr_buf
_audio_stream_read_buf
            READ_BIT_NO_SAVE_BUF
            rol audio_stream_next_addr_buf
            bcs _audio_stream_read_buf
            sta audio_stream_buf,x ; need to save buf
_audio_stream_read_return
            ldx audio_data_stream_idx
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
            ldy audio_stream_next_addr_hi
            bne _audio_jump_long
            lda audio_stream_next_addr_lo
            bpl _audio_jump_short
            dey ; y already 0
_audio_jump_short
            clc
            adc audio_stream_lo,x
            sta audio_stream_lo,x
            tya
            adc audio_stream_hi,x
            sta audio_stream_hi,x
            jmp _audio_jump_shift
_audio_jump_long
            sty audio_stream_hi,x
            lda audio_stream_next_addr_lo
            sta audio_stream_lo,x
_audio_jump_shift
            ldy audio_stream_next_addr_buf
            lda (audio_stream_ptr,x)
            sec
            ror
            dey
            bmi _audio_end_shift_loop
_audio_do_shift_loop
            lsr
            dey
            bpl _audio_do_shift_loop
_audio_end_shift_loop
_audio_skip_shift
            sta audio_stream_buf,x
            ldx audio_channel_idx
            jmp _audio_update_next_command   

SPAN_IDX
  byte 4,6

audio_decode_frequency
            audio_decode_frequency_MACRO
            sta audio_fx,x
            rts

; --- Canonical Huffman Decoder ---

_symbol_read_next_bit_shift
            iny
            lda CODEBOOK_LENGTHS,y
            bne _symbol_read_next_bit_skip
            iny
_symbol_read_next_bit_skip
            pla
            byte $2c
audio_stream_read_symbol:
            lda #1
_symbol_read_next_bit
            READ_BIT_SAVE_ACC
            rol 
            bmi _symbol_read_symbol
            cmp CODEBOOK_FIRST_VALUES,y
            bcc _symbol_read_next_bit
            pha
            asl
            cmp CODEBOOK_FIRST_VALUES+1,y
            bcs _symbol_read_next_bit_shift
            pla
_symbol_read_symbol
            adc CODEBOOK_LENGTHS,y
            sec
            sbc CODEBOOK_FIRST_VALUES,y
            tay
            lda CODEBOOK_CODES,y
            ldx audio_channel_idx
            rts

audio_data_stream_skip_address
            ldx audio_data_stream_idx
            lda audio_stream_buf,x
            READ_BIT_NO_SAVE_BUF
            ldy #(ADDRESS_INDEX_BITS - 1)
            bcc ._audio_skip_bits
            READ_BIT_NO_SAVE_BUF
            ldy #(11 - 1)
            bcc ._audio_skip_bits
            ldy #(ADDRESS_BITS - 1)
._audio_skip_bits
            READ_BIT_NO_SAVE_BUF
            dey
            bpl ._audio_skip_bits
            sta audio_stream_buf,x
            rts

    ENDM

    MAC READ_BIT_SAVE_ACC
            ; read one data bit from audio stream
            ; uses a sentinel bit and a few tricks picked up from
            ; http://forum.6502.org/viewtopic.php?f=2&t=4642    
            lsr audio_stream_buf,x
            bne ._audio_read_bit_end
            inc audio_stream_lo,x
            bne ._audio_read_bit_same_page
            inc audio_stream_hi,x
._audio_read_bit_same_page
            pha
            lda (audio_stream_ptr,x)
            sec ; set sentinel bit
            ror
            sta audio_stream_buf,x
            pla
._audio_read_bit_end
    ENDM

    MAC READ_BIT_NO_SAVE_BUF
            ; read one data bit from audio stream
            ; uses a sentinel bit and a few tricks picked up from
            ; http://forum.6502.org/viewtopic.php?f=2&t=4642    
            lsr 
            bne ._audio_read_bit_end    ; SPEED: takes way too many lines long when we inc twice
            inc audio_stream_lo,x
            bne ._audio_read_bit_same_page
            inc audio_stream_hi,x
._audio_read_bit_same_page
            lda (audio_stream_ptr,x)
            sec ; set sentinel bit
            ror
._audio_read_bit_end
    ENDM



NUM_SONGS = 1
NUM_CHANNELS = 2
STACK_DEPTH  = 2
LITERAL_DICTIONARY_SIZE = 128
SEQUENCE_DICTIONARY_SIZE = 64

audio_song              ds 1 ;
audio_stack             ds 2 * NUM_CHANNELS;
audio_timer             ds NUM_CHANNELS;

;
; sequence stack
;   11..xxxx yyyyyyyy push sequence at address $Fxyy
;   10xxxxxx          push sequence at dictionary 0 <= x < 63
;   0ddddddd          push literal sequence length d
;   00000000          pop
;
; literal section
;   1xxxxxxx          literal dictionary 0 < x < 127
;   011fffff ccccvvvv all registers
;   010fffff          frequency only
;   0010vvvv          volume only
;   0001cccc          channel only
;   0000dddd          d > 1, skip
;   00000000          pop
;

sub_inc_song
            lda audio_song
            clc
            adc #1
            cmp #NUM_SONGS
            bcc _inc_song_save
            lda #0
_inc_song_save
            sta audio_song
            rts

sub_dec_song
            lda audio_song
            sec
            sbc #1
            bcs _dec_song_save
            lda #(NUM_SONGS - 1)
_dec_song_save
            sta audio_song
            rts

sub_start_song
            ldy audio_song
            ldx audio_stack_0
            txs
            lda SONG_TABLE_0_START_HI,y
            pha
            lda SONG_TABLE_0_START_LO,y
            pha
            ldx audio_stack_1
            txs
            lda SONG_TABLE_1_START_HI,y
            pha
            lda SONG_TABLE_1_START_LO,y
            pha
            rts

sub_play_song
            ldx audio_timer
            beq _audio_continue
            dex
            stx audio_timer
            beq _audio_next_cmd
_audio_continue
_audio_end_song
            rts ; continue
_audio_next_cmd
            lda (audio_stream_ptr),y
            beq _audio_end_song
            lsr ; / 64
            lsr ;
            lsr ;
            lsr ;
            lsr ; 
            tax
            beq _audio_next_frame
            lda (audio_stream_ptr),y
            sta (AUDC0-1),x
            iny
            jmp _audio_next_cmd
_audio_next_frame
            lda (audio_stream_ptr),y
            and #$1f
            sta audio_timer
            tya 
            clc
            adc audio_stream_ptr
            sta audio_stream_ptr
            lda audio_stream_ptr + 1
            adc #0
            sta audio_stream_ptr + 1
            rts ; continue

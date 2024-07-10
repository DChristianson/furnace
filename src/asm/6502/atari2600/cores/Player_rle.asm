
NUM_SONGS = 1
NUM_CHANNELS = 2
STACK_DEPTH  = 2
LITERAL_DICTIONARY_SIZE = 128
SEQUENCE_DICTIONARY_SIZE = 64

audio_song              ds 1 ;
audio_stack             ds 2 * NUM_CHANNELS;
audio_timer             ds NUM_CHANNELS;

; coconut - 5k.fur file - all the volumes, all the frequencies, all the channels, few channel delta, skips <= 4
; spanish flea - 1k.fur file - all the volumes, half the frequencies, a few channels, skips <=4 
; breakbeat - 2k.fur file - all the volumes (delta master volume), all the frequencies, half the channels, skips <= 4
; hmove - 1k.fur file - half the volumes, half the frequencies, a few channels, skips <= 4
; 
; 
; ideas
; ----------------------
; straight register dump      - 2 bytes per channel
; delta compression           - 1 byte modifiers
; frequency envelope encoding - like TIA tracker
;
; compression techniques
; -----------------
; dictionary lookup
; subroutine compression
; huffman encoded 
; LZ arithmetic family
; using markov chain
; 
; schemes
;   command            -
;   command dictionary -
;   
;   duration        -
;   envelope        - duration, channel, attack, decay, sustain, release
;   m
;
; 0-3 sustain
;   5 bits 
; 
; 1111hhhh llllllll
;
; 0eefffff ccccvvvv 
;
; simple encoding:
; -----------------
; sssfffff ccccvvvv
; 
; delta literal encoding:
; -----------------
; 11sfffff ccccvvvv - embedded register values with sustain 0/1
; 10dddddd          - literal dictionary lookup 0-63
; 01sfffff          - change frequency with sustain 0/1
; 001svvvv          - change volume with sustain 0/1
; 0001siii          - chance instrument with sustain 0/1
; 0000ssss          - sustain 1-15
; 00000000          - end block
;
; delta sequence encoding
; ------------------
; 1111hhhh llllllll - sequence address lookup
; 10dddddd          - sequence dictionary lookup 0-63
; 110fffff ccccvvvv - embedded register values with sustain 0
; 1110vvvv          - embedded volume change with sustain 0
; 0lllllll          - literal block length 1-127
; 00000000          - end block
;
; combination literal encoding:
; -----------------
; 1111hhhh llllllll - repeat section at hhhh llllllll
; 10sfffff ccccvvvv - embedded register values with sustain 0-1
; 01dddddd          - dictionary lookup 0-63
; 001fffff          - change frequency 
; 0001vvvv          - change volume 
; 00001iii          - chance instrument 
; 00000sss          - sustain 1-8
; 00000000          - pop/end block
;
; command dictionary encoding:
; -----------------
; 1000ssss          - sustain 0-31 (1-32)
; 1001cccc          - change channel
; 101fffff ccccvvvv - embedded register values
; 110fffff          - embedded register values 
; 1110vvvv          - embedded register values
; 1111hhhh llllllll - branch to hhhh llllllll
; 0ddddddd          - command lookup 1-127
; 00000000          - pop/end block
; 00000000 00000000 - end song
; 
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

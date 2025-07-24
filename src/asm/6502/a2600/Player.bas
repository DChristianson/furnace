 rem ------------------------------------------------------------------------
 rem Tiacomp Demo
 rem ------------------------------------------------------------------------
 rem
 rem  This program shows how to use tiacomp audio.
 rem 

 rem ----------------------------------------------------------
 rem set ROM size to 8k for 2 banks of 4k each
 rem ----------------------------------------------------------
 set romsize 8k

start

  rem start audio play
 asm 
  jsr audio_play_track
end

main

 rem ----------------------------------------------------------
 rem Set color of playfield to Yellow, background to black
 rem ---------------------------------------------------------- 
 COLUPF=28
 COLUBK=0
 COLUP1=44

 playfield:
 ................................
 ................................
 ................................
 ................................
 ................................
 ................................
 ................................
 ................................
 ....X......................X....
 ...XX......................XX...
 ....X......................X....
 ................................
end

 rem ----------------------------------------------------------
 rem Call audio update
 rem ---------------------------------------------------------- 
 goto bank_audio_update bank2
bank_audio_update_return

 drawscreen

 goto main

 rem ----------------------------------------------------------
 rem Put audio controls in the main bank
 rem ---------------------------------------------------------- 
 asm

; use vars t to z for audio control
audio_track=t
audio_channel=u
audio_channel_0=u
audio_channel_1=w
audio_timer_0=y
audio_timer_1=z

  AUDIO_CONTROLS
  AUDIO_CONTROL_TABLE

end

 bank 2

 rem ----------------------------------------------------------
 rem Load songs into bank 2
 rem ---------------------------------------------------------- 
  
bank_audio_update
 asm
  jsr audio_update
end
  goto bank_audio_update_return bank1

 asm

  AUDIO_UPDATE

  include "Track_data.asm"
end
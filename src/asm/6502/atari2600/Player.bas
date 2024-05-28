 rem --------------
 rem batari basic sound
 rem based on SpiceWare's batari sfx driver
 rem    https://forums.atariage.com/topic/213203-sound-effect-driver/
 rem --------------
 
 rem allocate 2 variables for use by the audio player
 dim ax_channel_0 = y
 dim ax_channel_1 = z
 
 rem use the score to display selected sound effect
 dim ax_track = score + 2
 
 rem use A to debounce the firebutton (ie: only trigger 1 sound per press)
 dim debounce = a
 
 rem use B to slow down the up/down joystick processing
 dim up_down_delay = b
 
 rem score's used to display selected sound effect, 
 rem so set score color to make it visible
 scorecolor = $f : rem $f = white for both NTSC and PAL 

 rem --------------
 rem our main loop
 rem --------------

main
 up_down_delay = up_down_delay + 1
 if up_down_delay < 10 then goto check_fire
 up_down_delay = 0 
 
 if !joy0up then check_down
 temp1=ax_dec_track()
 
check_down
 if !joy0down then check_fire 
 temp1=ax_inc_track()
 
check_fire
 if joy0fire then goto fire_pressed
 debounce = 0
 goto draw_the_screen
 
fire_pressed
 if debounce then goto draw_the_screen
 debounce = 1
 temp1=ax_play_track() 
 
draw_the_screen 
 drawscreen 
 temp1=ax_update() : rem for best results, update sound effect after drawscreen
 goto main

 rem include player core and track data
 inline cores/ax_basic.asm
 inline Track_data.asm


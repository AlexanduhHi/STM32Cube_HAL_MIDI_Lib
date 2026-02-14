# STM32Cube HAL MIDI Library
A simple MIDI Input library for use with STM32CubeIDE utilizing CubeMX and HAL interfaces for U(S)ART Rx comms with DMA and callbacks.

__IMPORTANT HARDWARE INITIALIZATION INSTRUCTIONS:__

The STM32 UART hardware must be configured in a certain way in order for this MIDI library to
work properly. The UART you would like to use should be setup with a baudrate of 31,250 bps and
the UART must use DMA for Rx data. I recommend using CubeMX to assist with this setup.

The setup I had for testing this library (using USART6 on an STM32F746NG) used the following
CubeMX settings. With the desired UART selected, these are the tabs and their settings:

Parameter settings:
- UART mode = asynchronous
- Baudrate = 31250 bits/sec
- Word length = 8 bits
- Parity bits = none (MIDI doesn't use parity bits)
- Stop bits = 1
- Data direction = receive only

NVIC settings:
- both DMA and U(S)ART interrupts enabled

DMA settings:
- DMA request = U(S)ART#_RX
- Direction = peripheral to memory
- Mode = circular (implements a circular buffer)
- Increment address = Peripheral unchecked, memory checked (only increment the memory address)
- Data width = byte (for both peripheral and memory)

CubeMX usually handles the GPIO and clock considerations, but it's a good idea to double check.

CubeMX should automatically generate the necessary U(S)ART initialization code, and all you need
to do is pass the address of the UART handle to the MIDI_init function.
e.g. MIDI_init(&huart6, MIDI_CHANNEL_ALL);

__HOW TO USE THE MIDI LIBRARY:__

Simply run MIDI_init(huart,channel) before your main loop, passing it an STM32 HAL uart handle
and a MIDI channel to listen on. You can also specify MIDI_CHANNEL_ALL as the channel; this will
let the MIDI library listen to all 16 channels. Note: Currently, the library doesn't support
listening to more than one MIDI channel, nor does the library return the MIDI channel associated
with the received data. Effectively, the MIDI channel only acts as a filter, but additional
functionality may be added later.

Once the library is initialized with MIDI_init, you must run MIDI_check() periodically in your
main loop to parse the UART data when necessary. The library utilizes the HAL's UART interrupt
callback and sets a flag when this interrupt is called. No other MIDI-related activities occur
until the MIDI_check() function is called.

The library creates five user-definable callback functions, one for each main type of MIDI
message (we aren't counting SysEx as that is a whole other beast). The user (you!) can choose to
implement these however you wish. The library calls the appropriate callback function whenever a
message of the corresponding type is received:

- MIDI_noteOn(uint8_t note_num, uint8_t velocity)
	  called when a "Note On" command is received, and passes the 7-bit note number and velocity
	  value as arguments.
- MIDI_noteOff(uint8_t note_num, uint8_t velocity)
	  called when a "Note Off" command is received, and passes the 7-bit note number and velocity
	  value as arguments. Usually the note off velocity is zero, but the library makes no such
	  assumption.
- MIDI_CC(uint8_t control_num, uint8_t value)
	  called when a "Continuous Control" command is received, and passes the 7-bit control number
	  and corresponding value as arguments.
- MIDI_pitchBend(uint16_t pitchbend)
	  called when a "Pitchbend" command is received, and passes the 14-bit pitchbend value as an
	  argument.
- MIDI_systemReset()
	  called when a "System Reset" command is received. I've never seen this implemented but it is
	  important to have just in case. I recommend disabling all sounds/parameters/automation/etc
	  whenever this function is called, as it is intended as a panic button.

The MIDI data buffer is 128-bytes by default. You can override this by #define-ing
MIDI_BUFF_SIZE to whatever size you want (as long as it's divisible by two).

As a reminder, this library only implements MIDI input.

Please contact me if you have any questions, suggestions, or improvements.

/*
 * MIDI.c
 *
 * 	A simple MIDI Input library for STM32Cube. The library implements MIDI INPUT ONLY!
 * 	Utilizes STM32 HAL interfaces for receiving MIDI data from a U(S)ART with DMA.
 *
 *********************** IMPORTANT HARDWARE INITIALIZATION INSTRUCTIONS: ***************************
 * 	The STM32 UART hardware must be configured in a certain way in order for this MIDI library to
 * 	work properly. The UART you would like to use should be setup with a baudrate of 31,250 bps and
 * 	the UART must use DMA for Rx data. I recommend using CubeMX to assist with this setup.
 *
 * 	The setup I had for testing this library (using USART6 on an STM32F746NG) used the following
 * 	CubeMX settings. With the desired UART selected, these are the tabs and their settings:
 * 	Parameter settings:
 * 	- UART mode = asynchronous
 * 	- Baudrate = 31250 bits/sec
 * 	- Word length = 8 bits
 * 	- Parity bits = none (MIDI doesn't use parity bits)
 * 	- Stop bits = 1
 * 	- Data direction = receive only
 * 	NVIC settings:
 * 	- both DMA and U(S)ART interrupts enabled
 * 	DMA settings:
 * 	- DMA request = U(S)ART#_RX
 * 	- Direction = peripheral to memory
 * 	- Mode = circular (implements a circular buffer)
 * 	- Increment address = Peripheral unchecked, memory checked (only increment the memory address)
 *  - Data width = byte (for both peripheral and memory)
 *
 *  CubeMX usually handles the GPIO and clock considerations, but it's a good idea to double check.
 *
 *  CubeMX should automatically generate the necessary U(S)ART initialization code, and all you need
 *  to do is pass the address of the UART handle to the MIDI_init function.
 *  e.g. MIDI_init(&huart6, MIDI_CHANNEL_ALL);
 *
 ********************************** HOW TO USE THE MIDI LIBRARY ************************************
 * 	Simply run MIDI_init(huart,channel) before your main loop, passing it an STM32 HAL uart handle
 * 	and a MIDI channel to listen on. You can also specify MIDI_CHANNEL_ALL as the channel; this will
 * 	let the MIDI library listen to all 16 channels. Note: Currently, the library doesn't support
 * 	listening to more than one MIDI channel, nor does the library return the MIDI channel associated
 * 	with the received data. Effectively, the MIDI channel only acts as a filter, but additional
 * 	functionality may be added later.
 *
 * 	Once the library is initialized with MIDI_init, you must run MIDI_check() periodically in your
 * 	main loop to parse the UART data when necessary. The library utilizes the HAL's UART interrupt
 * 	callback and sets a flag when this interrupt is called. No other MIDI-related activities occur
 * 	until the MIDI_check() function is called.
 *
 * 	The library creates five user-definable callback functions, one for each main type of MIDI
 * 	message (we aren't counting SysEx as that is a whole other beast). The user (you!) can choose to
 * 	implement these however you wish. The library calls the appropriate callback function whenever a
 * 	message of the corresponding type is received:
 * 	- MIDI_noteOn(uint8_t note_num, uint8_t velocity)
 * 		called when a "Note On" command is received, and passes the 7-bit note number and velocity
 * 		value as arguments.
 * 	- MIDI_noteOff(uint8_t note_num, uint8_t velocity)
 * 		called when a "Note Off" command is received, and passes the 7-bit note number and velocity
 * 		value as arguments. Usually the note off velocity is zero, but the library makes no such
 * 		assumption.
 * 	- MIDI_CC(uint8_t control_num, uint8_t value)
 * 		called when a "Continuous Control" command is received, and passes the 7-bit control number
 * 		and corresponding value as arguments.
 * 	- MIDI_pitchBend(uint16_t pitchbend)
 * 		called when a "Pitchbend" command is received, and passes the 14-bit pitchbend value as an
 * 		argument.
 * 	- MIDI_systemReset()
 * 		called when a "System Reset" command is received. I've never seen this implemented but it is
 * 		important to have just in case. I recommend disabling all sounds/parameters/automation/etc
 * 		whenever this function is called, as it is intended as a panic button.
 *
 * 	The MIDI data buffer is 128-bytes by default. You can override this by #define-ing
 * 	MIDI_BUFF_SIZE to whatever size you want (as long as it's divisible by two).
 *
 * 	As a reminder, this library only implements MIDI input.
 *
 *  Created on: Feb 13, 2026
 *      Author: AlexanduhHi (Alex Elliott)
 */

#include "MIDI.h"

#ifndef MIDI_MAX_CMD_LEN
#define MIDI_MAX_CMD_LEN	8
#endif

#ifndef MIDI_BUFF_SIZE
#define MIDI_BUFF_SIZE		128
#endif

uint8_t MIDI_data_rcv;
uint8_t MIDI_rx_flag;
uint8_t MIDI_rx_half;

UART_HandleTypeDef* MIDI_uart; //uart pointer

uint8_t MIDI_buffer[MIDI_BUFF_SIZE]; //MIDI buffer
uint8_t MIDI_buffer_index; //index of current byte (initialized to 0)
uint8_t MIDI_message_length; //total number of bytes (including status) to expect
uint8_t MIDI_max_valid; //index of last valid byte in array (stop point for MIDI_check)
uint8_t MIDI_cmd_state; //FSM parameter for MIDI check (0: status byte, 1: data 1, 2: data 2, etc.)
uint8_t MIDI_cmd_stage[MIDI_MAX_CMD_LEN]; //staging area for MIDI command as bytes come in (always re-centered around status byte)
uint8_t MIDI_channel; //MIDI channel to listen to

/* MIDI_DATA_RX
 * @brief 	A "MIDI data received" callback, called by the HAL. Check the Rx status and set flags.
 * @param 	huart		The handle of the UART that received data and called the callback.
 * @param	Size		The size/amount of data that is received/valid.
 */
static void MIDI_DATA_RX(UART_HandleTypeDef* huart, uint16_t Size)
{
	if (huart->Instance == MIDI_uart->Instance) {
		if (huart->RxEventType == HAL_UART_RXEVENT_HT) {
			MIDI_rx_flag = 1; //first half ready
			MIDI_rx_half = 1;
			MIDI_max_valid = Size;
		}
		else if (huart->RxEventType == HAL_UART_RXEVENT_TC) {
			MIDI_rx_flag = 1; //second half ready
			MIDI_rx_half = 2;
			MIDI_max_valid = Size;
		}
		else if (huart->RxEventType == HAL_UART_RXEVENT_IDLE) {
			MIDI_rx_flag = 1; //received data ready (undetermined half)
			MIDI_max_valid = Size;
		}
	}
}

/* MIDI_parse
 * @brief 	Takes the completed MIDI command and interprets it, issuing the associated callback.
 */
static void MIDI_parse() {
	// What type of message did we receive? (Check status byte value.)
	// Call respective callback based on message received.
	uint8_t status_msb = MIDI_cmd_stage[0] >> 4;
	uint8_t status_lsb = MIDI_cmd_stage[0] & 0xF;
	if ((MIDI_channel == MIDI_CHANNEL_ALL) || (status_lsb == MIDI_channel) ) {
		if (status_msb == 0x8) {
			// NOTE OFF
			MIDI_noteOff(MIDI_cmd_stage[1] & 0x7F, MIDI_cmd_stage[2] & 0x7F);
			MIDI_cmd_state = 0; //reset buffer index in case of running status
		}
		else if (status_msb == 0x9) {
			// NOTE ON
			if (MIDI_cmd_stage[2] == 0) {
				// note_on velocity is 0 --> use Implicit Note Off
				MIDI_noteOff(MIDI_cmd_stage[1] & 0x7F, 0);
			}
			else {
				MIDI_noteOn(MIDI_cmd_stage[1] & 0x7F, MIDI_cmd_stage[2] & 0x7F);
			}
			MIDI_cmd_state = 0; //reset buffer index in case of running status
		}
		else if (status_msb == 0xB) {
			// CONTROL CHANGE (CC)
			MIDI_CC(MIDI_cmd_stage[1],MIDI_cmd_stage[2]);
			MIDI_cmd_state = 0; //reset buffer index in case of running status
		}
		else if (status_msb == 0xE) {
			// PITCH-BEND
			MIDI_pitchBend(((MIDI_cmd_stage[1] & 0x7F) | ((MIDI_cmd_stage[2] & 0x7F) << 7)) - 8192);
			MIDI_cmd_state = 0; //reset buffer index in case of running status
		}
		else {
			//not a supported MIDI message. ignore! :)
		}
	}
	else {
		//not the correct MIDI channel. ignore! :)
	}
}

/* MIDI_init
 * @brief 	Initializes the MIDI library with the given UART and MIDI channel.
 * @param 	huart		The handle of the UART to be used for MIDI input.
 * @param	channel		The MIDI channel to listen to, between 1 and 16. Alternatively, you can
 * 						specify "MIDI_CHANNEL_ALL" as the MIDI channel and that will let the library
 * 						listen to ALL 16 channels.
 */
void MIDI_init(UART_HandleTypeDef* huart, uint8_t channel) {
	MIDI_buffer_index = 0;
	MIDI_message_length = MIDI_BUFF_SIZE; //init the message length to the max allowable
	MIDI_uart = huart; //save the uart to listen to
	if ((channel > 0) && (channel <= 16)) {
		MIDI_channel = channel - 1; //channel should be between 1 and 16
	}
	else {
		MIDI_channel = MIDI_CHANNEL_ALL; //resort to listening to all channels if invalid channel provided
	}
	HAL_UART_RegisterRxEventCallback(huart, MIDI_DATA_RX); // register the user-defined RX callback
	HAL_UARTEx_ReceiveToIdle_DMA(huart, MIDI_buffer, MIDI_BUFF_SIZE); //start uart comms
}

/* MIDI_check
 * @brief 	Check if MIDI data was received. If data was received, organize it into discrete
 * 			commands and send them one by one to the MIDI parser. You MUST include this in the main
 * 			program loop somewhere to ensure MIDI data is continuously processed.
 */
void MIDI_check() {
	if (MIDI_rx_flag == 1) {
		// NEW DATA AVAILABLE, RUN STATE MACHINE!
		for (int i = MIDI_buffer_index; i < MIDI_max_valid; i++) {
			uint8_t new_byte = MIDI_buffer[i];
			if (new_byte == 0xFF) {
				// SYSTEM RESET: Used as a panic button. Makes all silent.
				MIDI_cmd_state = 0;
				MIDI_message_length = 0xFF; // prevent accidental parsing of a running status command after this
				MIDI_systemReset();
			}
			else if (new_byte >= 0x80) {
				//status byte
				MIDI_cmd_state = 0;
				// CHECK WHAT TYPE OF MESSAGE, TO SET CORRECT MESSAGE LENGTH
				uint8_t status_msb = new_byte >> 4;
				if ((status_msb == 0x8) || (status_msb == 0x9) || (status_msb == 0xA) || (status_msb == 0xB) || (status_msb == 0xE)) {
					MIDI_message_length = 2; // expect 2 data bytes
				}
				else if ((status_msb == 0xC) || (status_msb == 0xD)) {
					MIDI_message_length = 1; // expect 1 data byte
				}
				else if ((new_byte == 0xF1) || (new_byte == 0xF3)) {
					MIDI_message_length = 1; // expect 1 data byte
				}
				else if (new_byte == 0xF2) {
					MIDI_message_length = 2; // expect 2 data bytes
				}
				else {
					//not a supported MIDI message (probably SysEx)
					MIDI_message_length = 0xFF; // ensure the message parsing *never* occurs
				}
			}
			else {
				//data byte
				if (MIDI_cmd_state < MIDI_MAX_CMD_LEN) {
					MIDI_cmd_state++; //move FSM to next position for data byte
				}
				else {
					MIDI_cmd_state = 0;
				}
			}
			if (MIDI_cmd_state < MIDI_MAX_CMD_LEN) {
				MIDI_cmd_stage[MIDI_cmd_state] = new_byte;
			}
			if (MIDI_cmd_state >= MIDI_message_length) {
				// Command Is Complete! (in theory)
				// Parse This Command!
				MIDI_parse();
			}
		}
		MIDI_buffer_index = MIDI_max_valid; //reset buffer index to last byte read
		if (MIDI_buffer_index >= MIDI_BUFF_SIZE - 1) {
			MIDI_buffer_index = 0;
		}
		MIDI_rx_flag = 0; //reset MIDI RX flag
	}
}

// THE FOLLOWING ARE THE FIVE USER-DEFINABLE CALLBACKS MENTIONED IN THE DOCUMENTATION
// IMPLEMENT THESE ELSEWHERE IN YOUR CODE

__weak void MIDI_noteOn(uint8_t note_num, uint8_t velocity) { return; }

__weak void MIDI_noteOff(uint8_t note_num, uint8_t velocity) { return; }

__weak void MIDI_CC(uint8_t control_num, uint8_t value) { return; }

__weak void MIDI_pitchBend(uint16_t pitchbend) { return; }

__weak void MIDI_systemReset() { return; }

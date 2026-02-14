/*
 * MIDI.c
 *
 * Utilizes the "usart.h" module which handles USART6 on the STM32F746NG-DISCO board.
 * Receives MIDI commands/data on USART6 (Arduino connector pins PC7/D0)
 *
 *  Created on: Mar 20, 2024
 *      Author: Alex
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
			MIDI_rx_flag = 1; //receive ready (undetermined half)
			MIDI_max_valid = Size;
		}
	}
}

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

void MIDI_init(UART_HandleTypeDef* huart, uint8_t channel) {
	MIDI_buffer_index = 0;
	MIDI_message_length = MIDI_BUFF_SIZE; //init the message length to the max allowable
	HAL_UART_RegisterRxEventCallback(huart, MIDI_DATA_RX); // register the user-defined RX callback
	HAL_UARTEx_ReceiveToIdle_DMA(huart, MIDI_buffer, MIDI_BUFF_SIZE);
	MIDI_uart = huart; //save the uart to listen to
	if ((channel > 0) && (channel <= 16)) {
		MIDI_channel = channel - 1; //channel should be between 1 and 16
	}
	else {
		MIDI_channel = MIDI_CHANNEL_ALL; //resort to listening to all channels if invalid channel provided
	}
}

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
//			for (int i = 0; i < MIDI_BUFF_SIZE; i++)
//			{
//				MIDI_buffer[i] = 0;
//			}
		}
		MIDI_rx_flag = 0;
	}
}

//
//
// THE FOLLOWING ARE USER-DEFINABLE CALLBACKS

__weak void MIDI_noteOn(uint8_t note_num, uint8_t velocity) { return; }

__weak void MIDI_noteOff(uint8_t note_num, uint8_t velocity) { return; }

__weak void MIDI_CC(uint8_t control_num, uint8_t value) { return; }

__weak void MIDI_pitchBend(uint16_t pitchbend) { return; }

__weak void MIDI_systemReset() { return; }

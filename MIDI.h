/*
 * MIDI.h
 *
 *  Created on: Mar 20, 2024
 *      Author: Alex
 */

#ifndef INC_MIDI_H_
#define INC_MIDI_H_


#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */
#ifndef MIDI_CHANNEL_ALL
#define MIDI_CHANNEL_ALL	0xFF	//0xFF means listen to all channels
#endif
/* USER CODE END Private defines */

/* USER CODE BEGIN Prototypes */
void MIDI_init(UART_HandleTypeDef* huart, uint8_t channel);
void MIDI_check();

//USER-DEFINABLE CALLBACKS
void MIDI_noteOn(uint8_t, uint8_t);
void MIDI_noteOff(uint8_t, uint8_t);
void MIDI_CC(uint8_t, uint8_t);
void MIDI_pitchBend(uint16_t);
void MIDI_systemReset();
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif


#endif /* INC_MIDI_H_ */

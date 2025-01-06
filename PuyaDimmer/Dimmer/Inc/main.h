
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "py32f0xx_ll_rcc.h"
#include "py32f0xx_ll_bus.h"
#include "py32f0xx_ll_system.h"
#include "py32f0xx_ll_exti.h"
#include "py32f0xx_ll_cortex.h"
#include "py32f0xx_ll_utils.h"
#include "py32f0xx_ll_pwr.h"
#include "py32f0xx_ll_gpio.h"
#include "py32f0xx_ll_usart.h"
#include "py32f0xx_ll_tim.h"

#if defined(USE_FULL_ASSERT)
#include "py32_assert.h"
#endif /* USE_FULL_ASSERT */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);
void APP_UsartIRQCallback(void);
void APP_ZeroCrossingCallback(void);
void APP_Tim1UpdateCallback(void);

/* Dimmer stuff */

#define DIMMER_MODE_OFF		0
#define DIMMER_MODE_LEADING	1
#define DIMMER_MODE_TRAILING	2


// Get/report status: initialized, CH1, CH2
#define DIMMER_COMMAND_STATUS	1
// Init: Frequency, CH1 mode, CH1 transition, CH2 mode, CH2 transition
#define DIMMER_COMMAND_INIT	2
// Control: CH# ON/OFF brightness
#define DIMMER_COMMAND_CONTROL	3
// Control with custom fade
#define DIMMER_COMMAND_FADE	4


// dimmer state (with transition?)
typedef struct
{
  uint16_t level;
  uint16_t target_level;
  uint8_t increment;
  uint8_t cycles;
  //uint16_t start, end?
  // transition
  uint8_t mode;
} DimmerState;


// Instant on/off
#define TRANSITION_NONE		0
// Gradually fade in/out
#define TRANSITION_FADE		1
// Instant on/off + 2 seconds full speed if turning on from 0
#define TRANSITION_FAN		2
// Fade, but start with full brightness if turning on from 0
//#define TRANSITION_FULLFADE	3
// Fade, but start with full brightness if turning on from 0
//#define TRANSITION_HALFFADE	4




#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

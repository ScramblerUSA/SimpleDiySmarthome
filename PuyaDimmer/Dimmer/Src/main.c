
// Triac dimmer shuts down 1.6 ms into the next half-period due to current lag.
// 1.6 ms out of total 8 ms total for the half-period.

/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "settings.h"
#include "uart.h"

/* Private define ------------------------------------------------------------*/

// TIM1 Frequency mode
#define FREQUENCY_DETECT  0
#define FREQUENCY_50HZ    1
#define FREQUENCY_60HZ    2

// Main state
#define MAIN_STATE_POST_RESET           0
#define MAIN_STATE_FREQUENCY_DETECT     1
#define MAIN_STATE_WORK                 2
#define MAIN_STATE_ERROR                100

// Data points
#define DPID_1    0x01
#define DPID_2    0x02
#define DPID_21   0x10
#define DPID_22   0x20



/* Private variables ---------------------------------------------------------*/

volatile uint8_t g_mainState = 0;
volatile uint8_t g_acFrequency = 0;

// number of zero-crossings since startup
volatile uint32_t g_zcCount = 0;

volatile uint32_t g_configSaveTimer = 0;


uint8_t g_dpLightOn = 0;
uint16_t g_dpBrightness = 0;
uint8_t g_dpFanOn = 0;
uint8_t g_dpFanSpeed = 0;

volatile uint16_t g_lightCurrent = 0;
volatile uint16_t g_lightTarget = 0;
volatile uint8_t g_fanCurrent = 0;
volatile uint8_t g_fanTarget = 0;
volatile uint8_t g_fanDelay = 0;




const char *product_info = "{\"p\":\"SDS\",\"v\":\"1.0\"}";



/* Private function prototypes -----------------------------------------------*/
void APP_SystemClockConfig(void);
void APP_InitGPIO(void);
void APP_InitTIM1(uint8_t frequencyMode);
void APP_InitUSART(void);

void LoadSettings(void);
void ParseDatapointRequest(uint8_t dl, uint8_t *buf);
void SendDatapointUpdate(uint8_t dpids);
uint16_t GetFanDuty(uint32_t dp);
uint32_t GetZcTarget(uint32_t offset);
uint8_t ReachedZcCount(uint32_t target);


int main(void)
{
  uint32_t lastCommandTime = 0;
  uint8_t msg = 0;
  uint8_t dl = 0;
  uint8_t buf[UART_MAX_DATA_LENGTH];
  uint8_t heartbeatFlag = 0;

  while(1)
  {
    switch(g_mainState)
    {
    case MAIN_STATE_POST_RESET:
      APP_SystemClockConfig();
      APP_InitGPIO();
      APP_InitUSART();
      APP_InitTIM1(FREQUENCY_DETECT);
      LoadSettings();
      g_mainState = MAIN_STATE_FREQUENCY_DETECT;
      heartbeatFlag = 0;
      break;

    case MAIN_STATE_FREQUENCY_DETECT:
      if(g_acFrequency > 0)
      {
        if(g_acFrequency == 60)
          APP_InitTIM1(FREQUENCY_60HZ);
        else if(g_acFrequency == 50)
          APP_InitTIM1(FREQUENCY_50HZ);
        else
          Error_Handler();

        g_mainState = MAIN_STATE_WORK;
      }
      break;

    case MAIN_STATE_WORK:
      // process ESP control commands
      if(UartReadMessage(&msg, &dl, buf))
      {
        switch(msg)
        {
        case TUYA_CMD_HEARTBEAT:
          UartSendMessage(TUYA_CMD_HEARTBEAT, 1, &heartbeatFlag);
          heartbeatFlag = 1;
          break;
        case TUYA_CMD_PRODUCT:
          UartSendMessage(TUYA_CMD_PRODUCT, strlen(product_info), (uint8_t*)product_info);
          break;
        case TUYA_CMD_MODE:
          UartSendMessage(TUYA_CMD_MODE, 0, NULL);  // no dedicated status/reset pins
          break;
        case TUYA_CMD_WIFI:
          UartSendMessage(TUYA_CMD_WIFI, 0, NULL);
          break;
        case TUYA_CMD_DP_SET:
          ParseDatapointRequest(dl, buf);
          break;
        case TUYA_CMD_DP_QUERY:
          SendDatapointUpdate(0xFF);
          break;
        default:
          // just ignore the unnecessary command
          break;
        }
      }
      
      if(g_configSaveTimer != 0 && ReachedZcCount(g_configSaveTimer))
      {
        StoredSettings newset = {.brightness = g_dpBrightness, .fanSpeed = g_dpFanSpeed};
        SaveSettings(newset);
        g_configSaveTimer = 0;
      }
      
      break;

    default:
      Error_Handler();
    }
  }
}


void APP_SystemClockConfig(void)
{
  LL_RCC_HSI_Enable();
  LL_RCC_HSI_SetCalibFreq(LL_RCC_HSICALIBRATION_24MHz);
  while(LL_RCC_HSI_IsReady() != 1)
  {
  }

  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);

  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSISYS);
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSISYS)
  {
  }

  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
  LL_Init1msTick(24000000);
  
  LL_SetSystemCoreClock(24000000);

  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
  //LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);    // if port B (ext switches) is used
  LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_USART1);
  LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_TIM1);
}

void APP_InitGPIO(void)
{
  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  // PA2 - UART TX
  GPIO_InitStruct.Pin = LL_GPIO_PIN_2;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF1_USART1;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // PA3 - UART RX
  GPIO_InitStruct.Pin = LL_GPIO_PIN_3;
  GPIO_InitStruct.Alternate = LL_GPIO_AF1_USART1;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // PA0 = CH2  (TIM1_CH3)
  GPIO_InitStruct.Pin = LL_GPIO_PIN_0;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_13;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // PA1 = CH1  (TIM1_CH4)
  GPIO_InitStruct.Pin = LL_GPIO_PIN_1;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // PA5 - LED
  GPIO_InitStruct.Pin = LL_GPIO_PIN_5;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT; 
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_5);

  // PA4 = ZC detect
  GPIO_InitStruct.Pin = LL_GPIO_PIN_4;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT; 
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // PA6 = Btn
  //GPIO_InitStruct.Pin = LL_GPIO_PIN_6;
  //LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // PB2 = Sw2
  //GPIO_InitStruct.Pin = LL_GPIO_PIN_2;
  //LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // PB1 = Sw1
  //GPIO_InitStruct.Pin = LL_GPIO_PIN_1;
  //LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  LL_EXTI_InitTypeDef EXTI_InitStruct = {0};
  EXTI_InitStruct.Line = LL_EXTI_LINE_4;
  EXTI_InitStruct.LineCommand = ENABLE;
  EXTI_InitStruct.Mode = LL_EXTI_MODE_IT;
  EXTI_InitStruct.Trigger = LL_EXTI_TRIGGER_RISING_FALLING;
  LL_EXTI_Init(&EXTI_InitStruct);
  
  LL_EXTI_SetEXTISource(LL_EXTI_CONFIG_PORTA, LL_EXTI_CONFIG_LINE4);
  
  NVIC_SetPriority(EXTI4_15_IRQn, 0);
  NVIC_EnableIRQ(EXTI4_15_IRQn);
}

void APP_InitTIM1(uint8_t frequencyMode)
{
  LL_TIM_DisableCounter(TIM1);

  LL_TIM_InitTypeDef TIM1CountInit = {0};
  TIM1CountInit.ClockDivision       = LL_TIM_CLOCKDIVISION_DIV1;
  TIM1CountInit.CounterMode         = LL_TIM_COUNTERMODE_UP;
  switch(frequencyMode)
  {
  case FREQUENCY_DETECT:
    TIM1CountInit.Prescaler         = 1000 - 1;
    break;
  case FREQUENCY_50HZ:
    TIM1CountInit.Prescaler         = 240 - 1;
    break;
  case FREQUENCY_60HZ:
    TIM1CountInit.Prescaler         = 200 - 1;
    break;
  default:
    Error_Handler();
  }
  TIM1CountInit.Autoreload          = 60000 - 1;  // around 30 full waves to time-out
  TIM1CountInit.RepetitionCounter   = 0;
  LL_TIM_Init(TIM1, &TIM1CountInit);

  LL_TIM_SetUpdateSource(TIM1, LL_TIM_UPDATESOURCE_COUNTER);
  LL_TIM_ClearFlag_UPDATE(TIM1);
  LL_TIM_EnableIT_UPDATE(TIM1);
  LL_TIM_EnableCounter(TIM1);

  NVIC_EnableIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
  NVIC_SetPriority(TIM1_BRK_UP_TRG_COM_IRQn, 1);

  if(frequencyMode != FREQUENCY_DETECT)
  {
    LL_TIM_OC_InitTypeDef TIM_OC_Initstruct = {0};
    TIM_OC_Initstruct.OCState       = LL_TIM_OCSTATE_DISABLE;  // output is disabled for now
    TIM_OC_Initstruct.OCNState      = LL_TIM_OCSTATE_DISABLE;  // no complimentary outputs
    TIM_OC_Initstruct.OCPolarity    = LL_TIM_OCPOLARITY_HIGH;
    TIM_OC_Initstruct.OCIdleState   = LL_TIM_OCIDLESTATE_LOW;

    // CH1 is a trailing edge dimmer
    TIM_OC_Initstruct.OCMode        = LL_TIM_OCMODE_PWM1;
    TIM_OC_Initstruct.CompareValue  = 0;
    LL_TIM_OC_Init(TIM1, LL_TIM_CHANNEL_CH4, &TIM_OC_Initstruct);
    LL_TIM_OC_EnablePreload(TIM1, LL_TIM_CHANNEL_CH4);
    
    // CH2 is a leading edge fan controller
    TIM_OC_Initstruct.OCMode        = LL_TIM_OCMODE_PWM2;
    TIM_OC_Initstruct.CompareValue  = 1000;
    LL_TIM_OC_Init(TIM1, LL_TIM_CHANNEL_CH3, &TIM_OC_Initstruct);
    LL_TIM_OC_EnablePreload(TIM1, LL_TIM_CHANNEL_CH3);

    LL_TIM_EnableAllOutputs(TIM1);
  }
}

void APP_InitUSART(void)
{
  LL_USART_Disable(USART1);

  LL_USART_ConfigAsyncMode(USART1);

  LL_USART_InitTypeDef USART_InitStruct = {0};
  USART_InitStruct.BaudRate = 115200;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_1;
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(USART1, &USART_InitStruct);

  LL_USART_Enable(USART1);
  LL_USART_EnableIT_RXNE(USART1);

  NVIC_SetPriority(USART1_IRQn, 1);
  NVIC_EnableIRQ(USART1_IRQn);
}


void LoadSettings(void)
{
  g_dpBrightness = g_settings->brightness;
  g_dpFanSpeed = g_settings->fanSpeed;
}

uint16_t GetFanDuty(uint32_t dp)
{
  switch(dp)
  {
  case 1:
    //return 500;
    //return g_settings->fan1;
    return 700;
  case 2:
    //return 335;
    //return g_settings->fan2;
    return 535;
  case 3:
    //return 165;
    //return g_settings->fan3;
    return 365;
  case 4:
  default:
    return 0;
  }
}

void SendDatapointUpdate(uint8_t dpids)
{
  uint8_t buf[32];
  uint8_t size = 0;
  
  if(dpids & DPID_1)  // light on/off
  {
    buf[size++] = 1;  // DPID
    buf[size++] = 1;  // boolean
    buf[size++] = 0;
    buf[size++] = 1;  // length
    buf[size++] = g_dpLightOn;
  }
  
  if(dpids & DPID_2)  // light brightness
  {
    buf[size++] = 2;  // DPID
    buf[size++] = 2;  // integer
    buf[size++] = 0;
    buf[size++] = 4;  // length
    buf[size++] = 0;
    buf[size++] = 0;
    buf[size++] = (uint8_t)((g_dpBrightness >> 8) & 0xFF);
    buf[size++] = (uint8_t)(g_dpBrightness & 0xFF);
  }

  if(dpids & DPID_21)  // fan on/off
  {
    buf[size++] = 21; // DPID
    buf[size++] = 1;  // boolean
    buf[size++] = 0;
    buf[size++] = 1;  // length
    buf[size++] = g_dpFanOn;
  }
  
  if(dpids & DPID_22)  // fan speed
  {
    buf[size++] = 22; // DPID
    buf[size++] = 2;  // integer
    buf[size++] = 0;
    buf[size++] = 4;  // length
    buf[size++] = 0;
    buf[size++] = 0;
    buf[size++] = 0;
    buf[size++] = (uint8_t)(g_dpFanSpeed & 0xFF);
  }

  UartSendMessage(TUYA_CMD_DP_REPORT, size, buf);
}

void ParseDatapointRequest(uint8_t dl, uint8_t *buf)
{
  uint8_t dpid;
  uint8_t dptype;
  uint16_t dplen;
  uint8_t *dpval;
  uint16_t intval;

  uint8_t dpidsChanged = 0;
  uint8_t needSaveSettings = 0;
  
  while(dl >= 4)
  {
    dpid = buf[0];
    dptype = buf[1];
    dplen = (buf[2] << 8) + buf[3];
    dpval = &buf[4];
    if(dl < dplen + 4)
      break;  // not enough bytes in buffer for datapoint value (broken message)

    if(dpid == 1) dpidsChanged |= DPID_1;
    if(dpid == 2) dpidsChanged |= DPID_2;
    if(dpid == 21) dpidsChanged |= DPID_21;
    if(dpid == 22) dpidsChanged |= DPID_22;
    
    if((dpid == 1 || dpid == 21) && dptype == 1 && dplen == 1 && *dpval <= 1)  // DPs 1 and 21 are expected to be boolean with value 0 or 1
    {
      if(dpid == 1)
      {
        if(g_dpLightOn != *dpval)
          needSaveSettings = 1;
        g_dpLightOn = *dpval;
      }
      else
      {
        if(g_dpFanOn != *dpval)
          needSaveSettings = 1;
        g_dpFanOn = *dpval;
      }
    }
    
    if((dpid == 2 || dpid == 22) && dptype == 2 && dplen == 4 && dpval[0] == 0 && dpval[1] == 0)  // DPs 2 and 22 are expected to be integers with 2 higher bytes = 0
    {
      intval = (dpval[2] << 8) + dpval[3];
      if(dpid == 2)
      {
        if(intval >= 100 && intval <= 1000)
        {
          if(g_dpBrightness != intval)
            needSaveSettings = 1;
          g_dpBrightness = intval;
        }
      }
      else
      {
        if(intval >= 1 && intval <= 4)
        {
          if(g_dpFanSpeed != intval)
            needSaveSettings = 1;
          g_dpFanSpeed = intval;
        }
      }
    }

    //$$$ fan speed DPs
#if 0
    if((dpid == 31 || dpid == 32 || dpid == 33) && dptype == 2 && dplen == 4 && dpval[0] == 0 && dpval[1] == 0)  // DPs 31, 32, 33 are expected to be integers with 2 higher bytes = 0
    {
      intval = (dpval[2] << 8) + dpval[3];
      if(dpid == 2)
      {
        if(intval >= 100 && intval <= 1000)
        {
          if(g_dpBrightness != intval)
            needSaveSettings = 1;
          g_dpBrightness = intval;
        }
      }
      else
      {
        if(intval >= 1 && intval <= 4)
        {
          if(g_dpFanSpeed != intval)
            needSaveSettings = 1;
          g_dpFanSpeed = intval;
        }
      }
    }
#endif
    //$$$
  
    // locate to next DP if any
    buf += (dplen + 4);
    dl -= (dplen + 4);
  }

  if(dpidsChanged != 0)
  {
    SendDatapointUpdate(dpidsChanged);

    if(dpidsChanged & (DPID_1 | DPID_2))  // light settings changed
    {
      if(g_dpLightOn == 0)
        g_lightTarget = 0;
      else
        g_lightTarget = g_dpBrightness;
    }

    if(dpidsChanged & (DPID_21 | DPID_22)) // fan settings changed
    {
      if(g_dpFanOn == 0)
      {
        g_fanDelay = 0;    // turn off immediately even if we are in kick-start mode
        g_fanTarget = 0;
      }
      else
      {
        g_fanTarget = g_dpFanSpeed;
      }
    }
  }

  if(needSaveSettings)
    g_configSaveTimer = GetZcTarget(g_acFrequency * 10);    // schedule config save in 5 seconds
}



uint32_t GetZcTarget(uint32_t offset)
{
  uint32_t target = g_zcCount + offset;
  if(target == 0)
    target = 1;
  return target;
}

uint8_t ReachedZcCount(uint32_t target)
{
  // account for rollover of the target e.g. if current=0xFFFFFFF0 and target=current+100=0x54 -> target<current
  if(target <= g_zcCount && g_zcCount - target < 0x80000000)
    return 1;
  return 0;
}



void APP_UsartIRQCallback()
{
  if(LL_USART_IsActiveFlag_RXNE(USART1) && LL_USART_IsEnabledIT_RXNE(USART1))
  {
    g_rxBuffer[g_rxEnd] = LL_USART_ReceiveData8(USART1);
    g_rxEnd++;

    if(g_rxEnd >= UART_BUF_SIZE)
      g_rxEnd = 0;

    // check for buffer overflow
    if(g_rxEnd == g_rxStart)
    {
      // modify the last received byte to be 0
      uint8_t last = (g_rxEnd == 0) ? 99 : g_rxEnd - 1;
      g_rxBuffer[last] = 0;
    }
  }
  
  if(LL_USART_IsActiveFlag_TXE(USART1) && LL_USART_IsEnabledIT_TXE(USART1))
  {
    LL_USART_TransmitData8(USART1, g_txBuffer[g_txStart]);
    g_txStart++;
    if(g_txStart >= UART_BUF_SIZE)
      g_txStart = 0;

    if(g_txStart == g_txEnd)
    {
      LL_USART_DisableIT_TXE(USART1);
    }
  }
}

uint32_t zcTiming = 0;

void APP_ZeroCrossingCallback(void)
{
  // First order of business after zero crossing is detected in working state is to control outputs
  if(g_mainState == MAIN_STATE_WORK)
    LL_TIM_GenerateEvent_UPDATE(TIM1); // reset the timer to update outputs

  // count zero-crossings
  if(g_mainState >= MAIN_STATE_FREQUENCY_DETECT)
    g_zcCount++;

  if(g_mainState == MAIN_STATE_FREQUENCY_DETECT)
  {
    if(g_zcCount > 20 && g_zcCount < 25)  // skip first 20 transitions to stabilize
      zcTiming += TIM1->CNT;    // accumulate a sum of 4 half-wave durations

    if(g_zcCount == 25)
    {
      zcTiming = zcTiming / 4;  // average half-wave time

      if(zcTiming > 195 && zcTiming < 205)
        g_acFrequency = 60;
      else if(zcTiming > 235 && zcTiming < 245)
        g_acFrequency = 50;
      else
        g_acFrequency = 255;  // error
    }
    LL_TIM_GenerateEvent_UPDATE(TIM1); // reset the timer to update frequency counter
  }
  
  if(g_mainState == MAIN_STATE_WORK)
  {
    // process transitions
    if(g_lightCurrent != g_lightTarget)
    {
      if(g_lightCurrent < g_lightTarget)  // increasing brightness
      {
        if(g_lightCurrent < 10)
        {
          LL_TIM_CC_EnableChannel(TIM1, LL_TIM_CHANNEL_CH4);
          g_lightCurrent = 10;
        }
        else
        {
          g_lightCurrent += 5;

          if(g_lightCurrent > g_lightTarget)
            g_lightCurrent = g_lightTarget;
        }
        LL_TIM_OC_SetCompareCH4(TIM1, g_lightCurrent);
      }

      if(g_lightCurrent > g_lightTarget)  // decreasing brightness
      {
        g_lightCurrent -= 5;

        if(g_lightCurrent < g_lightTarget)
          g_lightCurrent = g_lightTarget;

        if(g_lightCurrent < 10)
        {
          g_lightCurrent = 0;
          LL_TIM_CC_DisableChannel(TIM1, LL_TIM_CHANNEL_CH4);
        }
        LL_TIM_OC_SetCompareCH4(TIM1, g_lightCurrent);
      }
    }

    if(g_fanTarget != g_fanCurrent)
    {
      if(g_fanCurrent == 0)
      {
        g_fanDelay = g_acFrequency * 4;  // 2-second full-speed kick for start-up
        g_fanCurrent = 4;
        LL_TIM_OC_SetCompareCH3(TIM1, GetFanDuty(g_fanCurrent));
        LL_TIM_CC_EnableChannel(TIM1, LL_TIM_CHANNEL_CH3);
      }

      if(g_fanDelay == 0)
      {
        g_fanCurrent = g_fanTarget;
        if(g_fanCurrent == 0)
          LL_TIM_CC_DisableChannel(TIM1, LL_TIM_CHANNEL_CH3);
        else
          LL_TIM_OC_SetCompareCH3(TIM1, GetFanDuty(g_fanCurrent));
      }
    }

    if(g_fanDelay != 0)
      g_fanDelay--;
  }
}

void APP_Tim1UpdateCallback(void)
{
  // Too much time without detecting a Zero-Crossing 
  NVIC_SystemReset();
}


void Error_Handler(void)
{
  g_mainState = MAIN_STATE_ERROR;
  while (1)
  {
    LL_GPIO_TogglePin(GPIOA, LL_GPIO_PIN_5);
    LL_mDelay(150);
  }
}

#ifdef  USE_FULL_ASSERT

void assert_failed(uint8_t *file, uint32_t line)
{
  g_mainState = MAIN_STATE_ERROR;
  // not looping here as asssert might be called from an interrupt
}

#endif /* USE_FULL_ASSERT */


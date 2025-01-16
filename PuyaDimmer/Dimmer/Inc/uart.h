
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __UART_H
#define __UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "py32f0xx_ll_system.h"
#include "py32f0xx_ll_usart.h"


// Uart buffer size
#define UART_BUF_SIZE      100

// UART ring buffers
extern uint8_t g_txBuffer[UART_BUF_SIZE];
extern uint8_t g_rxBuffer[UART_BUF_SIZE];
extern volatile uint8_t g_txStart;
extern volatile uint8_t g_txEnd;
extern volatile uint8_t g_rxStart;
extern volatile uint8_t g_rxEnd;


// UART functions
void UartSendByte(uint8_t bts);
void UartSendString(char *str);
uint8_t UartGetReceivedSize(void);
uint8_t UartPeekByte(uint8_t position);
void UartConsumeBytes(uint8_t count);
void UartSendMessage(uint8_t msg, uint8_t dlen, uint8_t *data);
uint8_t UartReadMessage(uint8_t *msg, uint8_t *dlen, uint8_t *data);

// Uart messages (Tuya-compatible)

#define TUYA_CMD_HEARTBEAT      0
#define TUYA_CMD_PRODUCT        1
#define TUYA_CMD_MODE           2
#define TUYA_CMD_WIFI           3
#define TUYA_CMD_DP_SET         6
#define TUYA_CMD_DP_REPORT      7
#define TUYA_CMD_DP_QUERY       8

// Message format:
// 55 AA VV CC LLLL [DD...DD] CS
// VV = version
// CC = command
// LLLL = data length
// DD = data bytes (depending on the message)
// CC = checksum = (sum of all bytes) % 256

// Max message data size
#define UART_MAX_DATA_LENGTH      64




#ifdef __cplusplus
}
#endif

#endif /* __UART_H */

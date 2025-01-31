
#include "uart.h"

uint8_t g_txBuffer[UART_BUF_SIZE];
uint8_t g_rxBuffer[UART_BUF_SIZE];
volatile uint8_t g_txStart = 0;
volatile uint8_t g_txEnd = 0;
volatile uint8_t g_rxStart = 0;
volatile uint8_t g_rxEnd = 0;

extern void Error_Handler(void);



void UartSendByte(uint8_t bts)
{
  g_txBuffer[g_txEnd] = bts;
  g_txEnd++;
  if(g_txEnd >= UART_BUF_SIZE)
    g_txEnd = 0;

  //Enable transmit data register empty interrupt
  LL_USART_EnableIT_TXE(USART1); 
}

// for debugging
void UartSendString(char *str)
{
  while(*str != 0)
  {
    UartSendByte(*str);
    str++;
  }
}

uint8_t UartGetReceivedSize(void)
{
  int8_t size = g_rxEnd - g_rxStart;
  if (size < 0)
    size += UART_BUF_SIZE;

  return size;
}

uint8_t UartPeekByte(uint8_t position)
{
  if(position < UartGetReceivedSize())
    return g_rxBuffer[(g_rxStart + position) % UART_BUF_SIZE];

  Error_Handler();
  return 0;
}

void UartConsumeBytes(uint8_t count)
{
  if(count > UartGetReceivedSize())
    Error_Handler();
  
  g_rxStart = (g_rxStart + count) % UART_BUF_SIZE;
}


void UartSendMessage(uint8_t msg, uint8_t dlen, uint8_t *data)
{
  // 55 AA VV CC 00LL [DD...DD] CS
  // VV = version (always 03)   CC = command   LL = data length   DD = data   CS = checksum

  uint16_t sum = 0xFF;  // sum of header bytes 55 AA

  UartSendByte(0x55);
  UartSendByte(0xAA);

  UartSendByte(0x03);  // version
  UartSendByte(msg);
  UartSendByte(0);     // len high byte
  UartSendByte(dlen);
  sum += msg + 3 + dlen;

  for(uint8_t i = 0; i < dlen; ++i)
  {
    UartSendByte(data[i]);
    sum += data[i];
  }

  UartSendByte((uint8_t)(sum & 0xFF));
}

uint8_t UartReadMessage(uint8_t *msg, uint8_t *dlen, uint8_t *data)
{
  uint16_t sum = 0xFF;  // sum of header bytes 55 AA
  uint8_t msize = UartGetReceivedSize();

  // message must be at least 7 bytes (2 bytes header, 1 byte version, 1 byte command, 2 byte data length, 1 byte checksum)
  if(msize < 7)
    return 0;

  if(UartPeekByte(0) != 0x55 || UartPeekByte(1) != 0xAA)
  {
    UartConsumeBytes(1);  // remove the byte from input buffer
    return 0;             // next read will start from the next byte
  }

  uint8_t ver, lenhigh;

  ver = UartPeekByte(2);  // don't care about the version, but need to include it in checksum calculation
  *msg = UartPeekByte(3);
  lenhigh = UartPeekByte(4);
  *dlen = UartPeekByte(5);
  if(lenhigh != 0 || *dlen > UART_MAX_DATA_LENGTH)  // we only support small messages
  {
    UartConsumeBytes(1);
    return 0;
  }
  sum += ver + *msg + *dlen;

  if(msize < 7 + *dlen)
    return 0;             // wait for the data bytes to arrive

  for(uint8_t i = 0; i < *dlen; ++i)
  {
    data[i] = UartPeekByte(6 + i);
    sum += data[i];
  }

  sum &= 0xFF;
  if(UartPeekByte(6 + *dlen) != sum)
  {
    UartConsumeBytes(7);  // bad CS = discard message, keep the body as the broken message might mask the second message
    return 0;
  }

  UartConsumeBytes(7 + *dlen);  // remove the entire message from input buffer
  return 1;
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

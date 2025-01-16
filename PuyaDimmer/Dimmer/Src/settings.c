
#include "main.h"
#include "settings.h"


#define SETTINGS_FLASH_ADDRESS          0x08001300

#define SETTINGS_FLASH_STRING1(x)       ".ARM.__at_" #x
#define SETTINGS_FLASH_STRING2(x)       SETTINGS_FLASH_STRING1(x)
#define SETTINGS_FLASH_SECTION          SETTINGS_FLASH_STRING2(SETTINGS_FLASH_ADDRESS)


typedef union
{
  uint32_t _words[32];  // words used to write to flash memory and to make sure nothing else gets allocated in this volatile page
  StoredSettings settings;

} SettingsPage;


// Settings reside at a fixed address in flash (at page boundary) occupying the entire page
// so page erase doesn't destroy any other data/code
#if defined ( __GNUC__ ) && !defined (__CC_ARM) /* GNU Compiler */
    const SettingsPage _settingsPage __attribute__((section(SETTINGS_FLASH_SECTION))) =
#elif defined (__CC_ARM)
    const SettingsPage _settingsPage __attribute__((at(SETTINGS_FLASH_ADDRESS))) =
#elif defined (__ICCARM__)
    __root const SettingsPage _settingsPage @ SETTINGS_FLASH_ADDRESS =
#else
    #error "unsupported compiler!!"
#endif
{
  .settings.brightness = 1000,
  .settings.fanSpeed = 4,
  .settings.fan1 = 700,
  .settings.fan2 = 535,
  .settings.fan3 = 365
};
    
    
// shortcut for settings within the page
const StoredSettings *g_settings = &_settingsPage.settings;




static uint8_t FlashUnlock(void);
static uint8_t FlashLock(void);
static void FlashErasePage(uint32_t address);
static uint8_t FlashCheckBlank(uint32_t address);
static void FlashWritePage(uint32_t address, uint32_t *data);


uint8_t SaveSettings(StoredSettings settings)
{
  SettingsPage page = {.settings = settings};
  char str[25];
  
  if(FlashUnlock() == 255)
    return 0;
  
  FlashErasePage(SETTINGS_FLASH_ADDRESS);
  
  if(FlashCheckBlank(SETTINGS_FLASH_ADDRESS) > 0)
    return 0;
  
  FlashWritePage(SETTINGS_FLASH_ADDRESS, page._words);

  if(FlashLock() == 255)
  {
    if(FlashLock() == 255)  // try again
      return 0;
  }
  
  return 1;
}






__STATIC_INLINE void WaitFlashBusy(void)
{
  while(READ_BIT(FLASH->SR, FLASH_SR_BSY) != 0)
  {}
}


static uint8_t FlashUnlock(void)
{
  WaitFlashBusy();
  if(READ_BIT(FLASH->CR, FLASH_CR_LOCK) == 0)
    return 0;  // already unlocked
    
  // Authorize the FLASH Registers access
  WRITE_REG(FLASH->KEYR, FLASH_KEY1);
  WRITE_REG(FLASH->KEYR, FLASH_KEY2);

  // verify Flash is unlocked
  if(READ_BIT(FLASH->CR, FLASH_CR_LOCK) != 0)
    return 255;  // error
    
  return 1;  // unlocked
}

static uint8_t FlashLock(void)
{
  WaitFlashBusy();
  SET_BIT(FLASH->CR, FLASH_CR_LOCK);

  // verify Flash is locked
  if(READ_BIT(FLASH->CR, FLASH_CR_LOCK) != 0)
    return 1;  // locked
  
  return 255;  // error
}

static void FlashErasePage(uint32_t address)
{
  WaitFlashBusy();
  SET_BIT(FLASH->CR, FLASH_CR_PER);
  *(__IO uint32_t *)(address) = 0xFF;
  WaitFlashBusy();
}

static uint8_t FlashCheckBlank(uint32_t address)
{
  uint8_t rc = 32;
  for(uint8_t i = 0; i < 32; ++i)
  {
    if(HW32_REG(address + 4 * i) == 0xFFFFFFFF)
      rc--;
  }
  return rc;
}

static void FlashWritePage(uint32_t address, uint32_t *data)
{
  uint32_t primask_bit;

  WaitFlashBusy();
  
  primask_bit = __get_PRIMASK();
  __disable_irq();
  SET_BIT(FLASH->CR, FLASH_CR_PG);

  for(uint8_t i = 0; i < 32; ++i)
  {
    if(i == 31)
      SET_BIT(FLASH->CR, FLASH_CR_PGSTRT);
    
    *(__IO uint32_t *)(address + 4 * i) = data[i];
  }

  WaitFlashBusy();
  
  CLEAR_BIT(FLASH->CR, FLASH_CR_PG);
  __set_PRIMASK(primask_bit);
}

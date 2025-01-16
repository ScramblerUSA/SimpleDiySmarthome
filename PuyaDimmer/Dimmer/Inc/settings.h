
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SETTINGS_H
#define __SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "py32f0xx_ll_system.h"



typedef struct
{
  uint16_t brightness;
  uint8_t fanSpeed;

  uint16_t fan1;
  uint16_t fan2;
  uint16_t fan3;

} StoredSettings;


extern const StoredSettings *g_settings;

uint8_t SaveSettings(StoredSettings settings);



#ifdef __cplusplus
}
#endif

#endif /* __SETTINGS_H */

#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_MEM_SIZE (32U * 1024U)
#define LV_DISP_DEF_REFR_PERIOD 30

#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

#define LV_USE_LOG 0

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1

#define LV_USE_BAR 1
#define LV_USE_LABEL 1
#define LV_USE_CANVAS 1
#define LV_USE_SPINNER 1

#endif

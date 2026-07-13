/*********************
 *      INCLUDES
 *********************/

#include "lvgl_port_touch.h"
#include "main.h"
#include "ft81x.h"

/**********************
 *  STATIC VARIABLES
 **********************/

static int32_t last_x = 0;
static int32_t last_y = 0;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void
lvgl_touchscreen_read (lv_indev_t *indev, lv_indev_data_t *data);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void
lvgl_touchscreen_init (void)
{
  /* Die kapazitive Touch-Engine des FT813 wurde in ft81x_init()
   * konfiguriert (Kompatibilitaetsmodus, kontinuierlich). Kein eigener
   * Controller, kein Interrupt-Pin: LVGL pollt REG_TOUCH_SCREEN_XY ueber
   * den (mit dem Display geteilten) SPI-Bus — Read-Callback und
   * Flush-Callback laufen beide im LVGL-Task, daher ohne Locking. */
  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, lvgl_touchscreen_read);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void
lvgl_touchscreen_read (lv_indev_t      *indev,
                       lv_indev_data_t *data)
{
  ft81x_touch_t point;

  LV_UNUSED(indev);

  if (ft81x_touch_read(&point) == FT81X_OK && point.pressed)
    {
      last_x = point.x;
      last_y = point.y;
      data->state = LV_INDEV_STATE_PRESSED;
    }
  else
    {
      data->state = LV_INDEV_STATE_RELEASED;
    }

  /* Bei Release die letzte Position beibehalten (LVGL-Konvention) */
  data->point.x = last_x;
  data->point.y = last_y;
}

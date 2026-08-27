#include <new>
#include <vector>
#include <time.h>
#include "bsp/esp-bsp.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Main"
#include "esp_lib_utils.h"
using namespace esp_brookesia;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems::phone;
extern "C" void app_main(void) {
 ESP_UTILS_LOGI("Starting ESP-Brookesia Toolbox on LCDWiki ES3C28P");
 const bsp_display_cfg_t cfg = BSP_DISPLAY_CONFIG_DEFAULT();
 ESP_UTILS_CHECK_NULL_EXIT(bsp_display_start_with_config(&cfg), "Display init failed");
 ESP_UTILS_CHECK_ERROR_EXIT(bsp_display_backlight_on(), "Backlight init failed");
 LvLock::registerCallbacks([](int ms) { if (ms < 0) ms = 0; else if (ms == 0) ms = 1; return bsp_display_lock(ms); }, []() { bsp_display_unlock(); return true; });
 Phone *phone = new (std::nothrow) Phone();
 ESP_UTILS_CHECK_NULL_EXIT(phone, "Could not create Phone");
 Stylesheet *sheet = new (std::nothrow) Stylesheet(STYLESHEET_320_240_DARK);
 ESP_UTILS_CHECK_NULL_EXIT(sheet, "Could not create stylesheet");
 sheet->core.name = "320x240 Compact Dark";
 sheet->display.status_bar.data.main.size.height = 30;
 sheet->display.status_bar.data.area.data[0].layout_column_start_offset = 8;
 sheet->display.status_bar.data.area.data[1].layout_column_start_offset = 8;
 sheet->display.status_bar.data.area.data[0].layout_column_pad = 2;
 sheet->display.status_bar.data.area.data[1].layout_column_pad = 2;
 sheet->display.navigation_bar.data.main.size.height = 34;
 sheet->display.app_launcher.data.icon.main.size = StyleSize::SQUARE(70);
 sheet->display.app_launcher.data.icon.main.layout_row_pad = 3;
 sheet->display.app_launcher.data.icon.image.default_size = StyleSize::SQUARE(48);
 sheet->display.app_launcher.data.icon.image.press_size = StyleSize::SQUARE(44);
 sheet->display.app_launcher.data.icon.label.text_font = StyleFont::SIZE(12);
 sheet->display.app_launcher.data.table.size = StyleSize::RECT_W_PERCENT(100, 170);
 sheet->display.app_launcher.data.indicator.main_size = StyleSize::RECT_W_PERCENT(100, 17);
 sheet->display.app_launcher.data.indicator.main_layout_column_pad = 8;
 sheet->display.app_launcher.data.indicator.main_layout_bottom_offset = 20;
 sheet->display.app_launcher.data.indicator.spot_inactive_size = StyleSize::SQUARE(10);
 sheet->display.app_launcher.data.indicator.spot_active_size = StyleSize::RECT(27, 10);
 ESP_UTILS_CHECK_FALSE_EXIT(phone->addStylesheet(sheet), "Could not add stylesheet");
 ESP_UTILS_CHECK_FALSE_EXIT(phone->activateStylesheet(sheet), "Could not activate stylesheet");
 delete sheet;
 { LvLockGuard guard;
  ESP_UTILS_CHECK_FALSE_EXIT(phone->begin(), "Phone begin failed");
  std::vector<systems::base::Manager::RegistryAppInfo> apps;
  ESP_UTILS_CHECK_FALSE_EXIT(phone->initAppFromRegistry(apps), "App registry init failed");
  ESP_UTILS_CHECK_FALSE_EXIT(phone->installAppFromRegistry(apps), "App install failed");
  lv_timer_create([](lv_timer_t *t) { time_t now; struct tm info; auto *p = static_cast<Phone *>(t->user_data); time(&now); localtime_r(&now, &info); p->getDisplay().getStatusBar()->setClock(info.tm_hour, info.tm_min); }, 1000, phone);
 }
 ESP_UTILS_LOGI("ESP-Brookesia Toolbox is ready");
}
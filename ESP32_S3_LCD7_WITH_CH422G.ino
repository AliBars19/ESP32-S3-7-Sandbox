/**
 * ESP32-S3-Touch-LCD-7 Hello World - Using Waveshare Library
 * 
 * This uses the Waveshare_ST7262_LVGL library which handles all
 * the complexity of CH422G, ST7262 initialization, and LVGL setup.
 * 
 * REQUIRED SETTINGS:
 * Tools > Board: "ESP32S3 Dev Module"
 * Tools > PSRAM: "OPI PSRAM"
 * Tools > Flash Size: "16MB"
 * 
 * REQUIRED LIBRARIES (Install from Library Manager):
 * - Waveshare_ST7262_LVGL
 * - LVGL 8.3.11
 * - ESP32_Display_Panel
 * - ESP32_IO_Expander
 */

#include <Arduino.h>
#include <Waveshare_ST7262_LVGL.h>
#include <lvgl.h>

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("========================================");
    Serial.println("ESP32-S3-LCD-7 Using Waveshare Library");
    Serial.println("========================================");
    
    // Initialize display and touchscreen using library
    Serial.println("Initializing display...");
    lcd_init();
    Serial.println("✓ Display initialized");
    
    // Lock LVGL mutex
    lvgl_port_lock(-1);
    
    // Create simple UI
    Serial.println("Creating UI...");
    
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    
    // Main title
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "HELLO WORLD!");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_center(title);
    
    // Success message
    lv_obj_t *status = lv_label_create(scr);
    lv_label_set_text(status, "IT WORKS!");
    lv_obj_set_style_text_color(status, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_28, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 80);
    
    // Board info
    lv_obj_t *info = lv_label_create(scr);
    lv_label_set_text(info, "Waveshare ESP32-S3-Touch-LCD-7");
    lv_obj_set_style_text_color(info, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_20, 0);
    lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -30);
    
    // Unlock LVGL mutex
    lvgl_port_unlock();
    
    Serial.println("========================================");
    Serial.println("✓✓✓ SUCCESS! ✓✓✓");
    Serial.println("========================================");
}

void loop() {
    // Library handles LVGL timer internally
    delay(10);
}

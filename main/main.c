#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "water_pump";

// GPIO pins for L9110 motor driver
#define MOTOR_PIN_IA    GPIO_NUM_25   // PWM pin (speed control)
#define MOTOR_PIN_IB    GPIO_NUM_26   // Direction pin

// LEDC (PWM) configuration
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_FREQ_HZ    1000          // 1 kHz PWM frequency
#define LEDC_RESOLUTION LEDC_TIMER_8_BIT  // 0-255 duty range

// Set pump speed (0-255)
void pump_set_speed(uint8_t speed)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, speed);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    ESP_LOGI(TAG, "Pump speed set to: %d", speed);
}

// Stop the pump
void pump_stop(void)
{
    pump_set_speed(0);
    ESP_LOGI(TAG, "Pump stopped");
}

// Initialize PWM for motor control
void pump_init(void)
{
    // Configure LEDC timer
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_RESOLUTION,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_conf);

    // Configure LEDC channel for IA (PWM)
    ledc_channel_config_t channel_conf = {
        .gpio_num = MOTOR_PIN_IA,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&channel_conf);

    // Configure IB as regular GPIO (set LOW for forward direction)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MOTOR_PIN_IB),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(MOTOR_PIN_IB, 0);  // LOW for forward

    ESP_LOGI(TAG, "Pump initialized on GPIO %d (PWM) and GPIO %d (DIR)",
             MOTOR_PIN_IA, MOTOR_PIN_IB);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Water Pump Controller Starting...");

    pump_init();

    // Demo: cycle through different speeds
    while (1) {
        ESP_LOGI(TAG, "--- Speed: OFF ---");
        pump_stop();
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "--- Speed: LOW (25%%) ---");
        pump_set_speed(64);
        vTaskDelay(pdMS_TO_TICKS(3000));

        ESP_LOGI(TAG, "--- Speed: MEDIUM (50%%) ---");
        pump_set_speed(128);
        vTaskDelay(pdMS_TO_TICKS(3000));

        ESP_LOGI(TAG, "--- Speed: HIGH (75%%) ---");
        pump_set_speed(192);
        vTaskDelay(pdMS_TO_TICKS(3000));

        ESP_LOGI(TAG, "--- Speed: MAX (100%%) ---");
        pump_set_speed(255);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

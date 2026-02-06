#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_server.h"

static const char *TAG = "plant_doctor";

// GPIO pins for L9110 motor driver
#define MOTOR_PIN_IA    GPIO_NUM_25   // PWM pin (speed control)
#define MOTOR_PIN_IB    GPIO_NUM_26   // Direction pin

// LEDC (PWM) configuration
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_FREQ_HZ    1000          // 1 kHz PWM frequency
#define LEDC_RESOLUTION LEDC_TIMER_8_BIT  // 0-255 duty range

// WiFi AP configuration
#define WIFI_SSID       "PlantDoc"
#define WIFI_CHANNEL    1
#define MAX_STA_CONN    4

// Set pump speed (0-255)
void pump_set_speed(uint8_t speed)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, speed);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    ESP_LOGI(TAG, "Pump speed set to: %d (%.0f%%)", speed, (speed / 255.0) * 100);
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

// HTML page with embedded CSS and JavaScript
static const char HTML_PAGE[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>PlantDoc AI</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #1a4a2e 0%, #0d2818 100%);
            min-height: 100vh;
            color: #fff;
            padding: 20px;
        }
        .container { max-width: 500px; margin: 0 auto; }
        h1 {
            text-align: center;
            font-size: 28px;
            margin-bottom: 10px;
            text-shadow: 0 2px 4px rgba(0,0,0,0.3);
        }
        .subtitle {
            text-align: center;
            color: #8fbc8f;
            margin-bottom: 30px;
            font-size: 14px;
        }
        .card {
            background: rgba(255,255,255,0.1);
            border-radius: 16px;
            padding: 20px;
            margin-bottom: 20px;
            backdrop-filter: blur(10px);
            border: 1px solid rgba(255,255,255,0.1);
        }
        .card-title {
            font-size: 16px;
            color: #8fbc8f;
            margin-bottom: 15px;
        }
        .plant-options {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 15px;
        }
        .plant-btn {
            background: rgba(255,255,255,0.15);
            border: 2px solid transparent;
            border-radius: 12px;
            padding: 20px 15px;
            cursor: pointer;
            transition: all 0.3s ease;
            text-align: center;
        }
        .plant-btn:hover {
            background: rgba(255,255,255,0.25);
            border-color: #4ade80;
            transform: translateY(-2px);
        }
        .plant-btn:active { transform: translateY(0); }
        .plant-btn.disabled {
            opacity: 0.5;
            pointer-events: none;
        }
        .plant-icon { font-size: 48px; margin-bottom: 10px; }
        .plant-name { font-size: 14px; font-weight: 600; }
        .plant-file { font-size: 11px; color: #8fbc8f; margin-top: 5px; }

        #processing {
            display: none;
            text-align: center;
            padding: 30px 20px;
        }
        #processing.active { display: block; }
        .spinner {
            width: 60px;
            height: 60px;
            border: 4px solid rgba(255,255,255,0.2);
            border-top-color: #4ade80;
            border-radius: 50%;
            animation: spin 1s linear infinite;
            margin: 0 auto 20px;
        }
        @keyframes spin { to { transform: rotate(360deg); } }
        #stage-text {
            font-size: 16px;
            color: #4ade80;
            min-height: 24px;
        }
        .progress-bar {
            background: rgba(255,255,255,0.1);
            border-radius: 10px;
            height: 8px;
            margin-top: 20px;
            overflow: hidden;
        }
        .progress-fill {
            background: linear-gradient(90deg, #4ade80, #22c55e);
            height: 100%;
            width: 0%;
            transition: width 0.3s ease;
        }

        #result {
            display: none;
        }
        #result.active { display: block; }
        .result-header {
            display: flex;
            align-items: center;
            gap: 15px;
            margin-bottom: 20px;
        }
        .result-icon { font-size: 50px; }
        .result-title { font-size: 20px; font-weight: 700; }
        .result-plant { font-size: 13px; color: #8fbc8f; }

        .info-row {
            display: flex;
            justify-content: space-between;
            padding: 12px 0;
            border-bottom: 1px solid rgba(255,255,255,0.1);
        }
        .info-row:last-child { border-bottom: none; }
        .info-label { color: #8fbc8f; font-size: 13px; }
        .info-value { font-weight: 600; font-size: 14px; }
        .info-value.disease { color: #f87171; }
        .info-value.treatment { color: #4ade80; }

        .status-badge {
            display: inline-flex;
            align-items: center;
            gap: 8px;
            background: rgba(74, 222, 128, 0.2);
            padding: 8px 16px;
            border-radius: 20px;
            margin-top: 15px;
            font-size: 13px;
        }
        .status-badge.spraying { background: rgba(74, 222, 128, 0.3); }
        .pulse {
            width: 10px;
            height: 10px;
            background: #4ade80;
            border-radius: 50%;
            animation: pulse 1.5s infinite;
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; transform: scale(1); }
            50% { opacity: 0.5; transform: scale(1.2); }
        }

        .btn-row {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 10px;
            margin-top: 20px;
        }
        .btn {
            padding: 14px 20px;
            border: none;
            border-radius: 10px;
            font-size: 14px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s ease;
        }
        .btn-stop {
            background: #ef4444;
            color: white;
        }
        .btn-stop:hover { background: #dc2626; }
        .btn-new {
            background: rgba(255,255,255,0.2);
            color: white;
        }
        .btn-new:hover { background: rgba(255,255,255,0.3); }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌿 PlantDoc AI</h1>
        <p class="subtitle">AI-Powered Plant Disease Detection</p>

        <div class="card" id="selection">
            <div class="card-title">Select Plant to Analyze</div>
            <div class="plant-options">
                <div class="plant-btn" onclick="analyze('sugarcane')">
                    <div class="plant-icon">🌾</div>
                    <div class="plant-name">Sugarcane</div>
                    <div class="plant-file">1.jpg</div>
                </div>
                <div class="plant-btn" onclick="analyze('tomato')">
                    <div class="plant-icon">🍅</div>
                    <div class="plant-name">Tomato</div>
                    <div class="plant-file">2.jpg</div>
                </div>
            </div>
        </div>

        <div class="card" id="processing">
            <div class="spinner"></div>
            <div id="stage-text">Initializing...</div>
            <div class="progress-bar">
                <div class="progress-fill" id="progress"></div>
            </div>
        </div>

        <div class="card" id="result">
            <div class="result-header">
                <div class="result-icon" id="result-icon">🌾</div>
                <div>
                    <div class="result-title" id="result-title">Analysis Complete</div>
                    <div class="result-plant" id="result-plant">Sugarcane Leaf</div>
                </div>
            </div>

            <div class="info-row">
                <span class="info-label">Disease Detected</span>
                <span class="info-value disease" id="disease-name">-</span>
            </div>
            <div class="info-row">
                <span class="info-label">Confidence</span>
                <span class="info-value" id="confidence">-</span>
            </div>
            <div class="info-row">
                <span class="info-label">Recommended Treatment</span>
                <span class="info-value treatment" id="treatment">-</span>
            </div>
            <div class="info-row">
                <span class="info-label">Spray Intensity</span>
                <span class="info-value" id="pwm-value">-</span>
            </div>

            <div class="status-badge spraying" id="spray-status">
                <div class="pulse"></div>
                <span>Spraying in progress...</span>
            </div>

            <div class="btn-row">
                <button class="btn btn-stop" onclick="stopPump()">⏹ Stop Spray</button>
                <button class="btn btn-new" onclick="newAnalysis()">🔄 New Analysis</button>
            </div>
        </div>
    </div>

    <script>
        const stages = [
            { text: "📸 Capturing frame...", duration: 500, progress: 12 },
            { text: "🔍 Detecting plant regions...", duration: 800, progress: 30 },
            { text: "🧬 Analyzing leaf patterns...", duration: 1200, progress: 55 },
            { text: "🤖 Running AI model...", duration: 1500, progress: 85 },
            { text: "✅ Disease identified!", duration: 300, progress: 100 }
        ];

        const diseases = {
            sugarcane: {
                icon: "🌾",
                plant: "Sugarcane Leaf",
                disease: "Red Rot",
                confidence: "94.7%",
                treatment: "Chlorantraniliprole",
                pwm: 128,
                pwmPercent: "50%"
            },
            tomato: {
                icon: "🍅",
                plant: "Tomato Leaf",
                disease: "Early Blight",
                confidence: "91.2%",
                treatment: "Mancozeb / Chlorothalonil",
                pwm: 179,
                pwmPercent: "70%"
            }
        };

        let currentPlant = null;

        async function analyze(plant) {
            currentPlant = plant;

            // Disable buttons
            document.querySelectorAll('.plant-btn').forEach(b => b.classList.add('disabled'));

            // Show processing
            document.getElementById('selection').style.display = 'none';
            document.getElementById('processing').classList.add('active');
            document.getElementById('result').classList.remove('active');

            // Run through stages
            for (let i = 0; i < stages.length; i++) {
                document.getElementById('stage-text').textContent = stages[i].text;
                document.getElementById('progress').style.width = stages[i].progress + '%';
                await sleep(stages[i].duration);
            }

            // Show result
            await sleep(300);
            showResult(plant);
        }

        async function showResult(plant) {
            const data = diseases[plant];

            document.getElementById('result-icon').textContent = data.icon;
            document.getElementById('result-plant').textContent = data.plant;
            document.getElementById('disease-name').textContent = data.disease;
            document.getElementById('confidence').textContent = data.confidence;
            document.getElementById('treatment').textContent = data.treatment;
            document.getElementById('pwm-value').textContent = data.pwmPercent + ' (PWM: ' + data.pwm + ')';

            document.getElementById('processing').classList.remove('active');
            document.getElementById('result').classList.add('active');
            document.getElementById('spray-status').style.display = 'inline-flex';

            // Start spray
            try {
                await fetch('/spray?pwm=' + data.pwm);
            } catch(e) {
                console.error('Spray request failed:', e);
            }
        }

        async function stopPump() {
            try {
                await fetch('/stop');
                document.getElementById('spray-status').style.display = 'none';
            } catch(e) {
                console.error('Stop request failed:', e);
            }
        }

        function newAnalysis() {
            document.getElementById('selection').style.display = 'block';
            document.getElementById('processing').classList.remove('active');
            document.getElementById('result').classList.remove('active');
            document.getElementById('progress').style.width = '0%';
            document.querySelectorAll('.plant-btn').forEach(b => b.classList.remove('disabled'));
            currentPlant = null;
        }

        function sleep(ms) {
            return new Promise(resolve => setTimeout(resolve, ms));
        }
    </script>
</body>
</html>
)rawliteral";

// HTTP GET handler for root page
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_PAGE, strlen(HTML_PAGE));
    return ESP_OK;
}

// HTTP GET handler for spray endpoint
static esp_err_t spray_get_handler(httpd_req_t *req)
{
    char buf[100];
    int buf_len = httpd_req_get_url_query_len(req) + 1;

    if (buf_len > 1 && buf_len < sizeof(buf)) {
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[32];
            if (httpd_query_key_value(buf, "pwm", param, sizeof(param)) == ESP_OK) {
                int pwm = atoi(param);
                if (pwm >= 0 && pwm <= 255) {
                    pump_set_speed((uint8_t)pwm);
                    ESP_LOGI(TAG, "Spray started with PWM: %d", pwm);
                }
            }
        }
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// HTTP GET handler for stop endpoint
static esp_err_t stop_get_handler(httpd_req_t *req)
{
    pump_stop();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// Start HTTP server
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        // Root page
        httpd_uri_t root = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &root);

        // Spray endpoint
        httpd_uri_t spray = {
            .uri       = "/spray",
            .method    = HTTP_GET,
            .handler   = spray_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &spray);

        // Stop endpoint
        httpd_uri_t stop = {
            .uri       = "/stop",
            .method    = HTTP_GET,
            .handler   = stop_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &stop);

        ESP_LOGI(TAG, "HTTP server started");
    }

    return server;
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station connected - MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5]);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station disconnected - MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5]);
    }
}

// Initialize WiFi as Access Point
static void wifi_init_ap(void)
{
    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // Configure AP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .password = "",
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started. SSID: %s", WIFI_SSID);
    ESP_LOGI(TAG, "Connect to WiFi and open http://192.168.4.1");
}

void app_main(void)
{
    ESP_LOGI(TAG, "PlantDoc AI - Plant Disease Detection System");
    ESP_LOGI(TAG, "============================================");

    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize pump
    pump_init();
    pump_stop();  // Ensure pump is off at startup

    // Initialize WiFi AP
    wifi_init_ap();

    // Start HTTP server
    start_webserver();

    ESP_LOGI(TAG, "System ready!");
    ESP_LOGI(TAG, "1. Connect to WiFi: PlantDoc");
    ESP_LOGI(TAG, "2. Open browser: http://192.168.4.1");
    ESP_LOGI(TAG, "3. Select plant to analyze");
}

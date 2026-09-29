#include "wifi_board.h"
#include "adc_pdm_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "settings.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <vector>

#include "display/lcd_display.h"
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include "esp_lcd_ili9341.h"

#include "assets/lang_config.h"
#include "anim_player.h"
#include "dog_motion.h"
#include "emoji_display.h"
#include "servo_dog_ctrl.h"
#include "led_strip.h"
#include "driver/rmt_tx.h"
#include "device_state.h"

#include "sdkconfig.h"

#ifdef CONFIG_ESP_HI_WEB_CONTROL_ENABLED
#include "esp_hi_web_control.h"
#endif //CONFIG_ESP_HI_WEB_CONTROL_ENABLED

#define TAG "ESP_HI"

// Named static payloads: C++ compound-literal pointers must outlive panel init.
static const uint8_t st7735_cmd_b1[] = {0x05, 0x3A, 0x3A};
static const uint8_t st7735_cmd_b2[] = {0x05, 0x3A, 0x3A};
static const uint8_t st7735_cmd_b3[] = {0x05, 0x3A, 0x3A, 0x05, 0x3A, 0x3A};
static const uint8_t st7735_cmd_b4[] = {0x03};
static const uint8_t st7735_cmd_c0[] = {0x44, 0x04, 0x04};
static const uint8_t st7735_cmd_c1[] = {0xC0};
static const uint8_t st7735_cmd_c2[] = {0x0D, 0x00};
static const uint8_t st7735_cmd_c3[] = {0x8D, 0x6A};
static const uint8_t st7735_cmd_c4[] = {0x8D, 0xEE};
static const uint8_t st7735_cmd_c5[] = {0x08};
static const uint8_t st7735_cmd_e0[] = {0x0F, 0x10, 0x03, 0x03, 0x07, 0x02, 0x00, 0x02,
                                        0x07, 0x0C, 0x13, 0x38, 0x0A, 0x0E, 0x03, 0x10};
static const uint8_t st7735_cmd_e1[] = {0x10, 0x0B, 0x04, 0x04, 0x10, 0x03, 0x00, 0x03,
                                        0x03, 0x09, 0x17, 0x33, 0x0B, 0x0C, 0x06, 0x10};
static const uint8_t st7735_cmd_35[] = {0x00};
static const uint8_t st7735_cmd_3a[] = {0x05};
static const uint8_t st7735_cmd_36[] = {0xC8};

static const ili9341_lcd_init_cmd_t vendor_specific_init[] = {
    {0x11, NULL, 0, 120},  // Sleep out, Delay 120ms
    {0xB1, st7735_cmd_b1, sizeof(st7735_cmd_b1), 0},
    {0xB2, st7735_cmd_b2, sizeof(st7735_cmd_b2), 0},
    {0xB3, st7735_cmd_b3, sizeof(st7735_cmd_b3), 0},
    {0xB4, st7735_cmd_b4, sizeof(st7735_cmd_b4), 0},  // Dot inversion
    {0xC0, st7735_cmd_c0, sizeof(st7735_cmd_c0), 0},
    {0xC1, st7735_cmd_c1, sizeof(st7735_cmd_c1), 0},
    {0xC2, st7735_cmd_c2, sizeof(st7735_cmd_c2), 0},
    {0xC3, st7735_cmd_c3, sizeof(st7735_cmd_c3), 0},
    {0xC4, st7735_cmd_c4, sizeof(st7735_cmd_c4), 0},
    {0xC5, st7735_cmd_c5, sizeof(st7735_cmd_c5), 0},
    {0xE0, st7735_cmd_e0, sizeof(st7735_cmd_e0), 0},
    {0xE1, st7735_cmd_e1, sizeof(st7735_cmd_e1), 0},
    {0x35, st7735_cmd_35, sizeof(st7735_cmd_35), 0},
    {0x3A, st7735_cmd_3a, sizeof(st7735_cmd_3a), 0},
    {0x36, st7735_cmd_36, sizeof(st7735_cmd_36), 0},
    {0x29, NULL, 0, 0},  // Display on
    {0x2C, NULL, 0, 0},  // Memory write
};

static const led_strip_config_t bsp_strip_config = {
    .strip_gpio_num = GPIO_NUM_8,
    .max_leds = 4,
    .led_model = LED_MODEL_WS2812,
    .flags = {
        .invert_out = false
    }
};

static const led_strip_rmt_config_t bsp_rmt_config = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 10 * 1000 * 1000,
    .flags = {
        .with_dma = false
    }
};

class EspHi : public WifiBoard {
private:
    Button boot_button_;
    Button audio_wake_button_;
    Button move_wake_button_;
    anim::EmojiWidget* display_ = nullptr;
    bool web_server_initialized_ = false;
    led_strip_handle_t led_strip_;
    bool led_on_ = false;

    void InitializeLegacyDeviceIdentity()
    {
        Settings settings("dev_identity", false);
        const std::string custom_mac = settings.GetString("custom_mac");
        if (custom_mac.empty()) {
            return;
        }

        unsigned int octets[6] = {};
        if (sscanf(custom_mac.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", &octets[0], &octets[1],
                   &octets[2], &octets[3], &octets[4], &octets[5]) != 6) {
            ESP_LOGW(TAG, "Ignoring invalid legacy custom MAC: %s", custom_mac.c_str());
            return;
        }

        uint8_t mac[6];
        bool all_zero = true;
        for (size_t i = 0; i < sizeof(mac); ++i) {
            if (octets[i] > UINT8_MAX) {
                ESP_LOGW(TAG, "Ignoring invalid legacy custom MAC: %s", custom_mac.c_str());
                return;
            }
            mac[i] = static_cast<uint8_t>(octets[i]);
            all_zero = all_zero && mac[i] == 0;
        }
        if (all_zero || (mac[0] & 0x01) != 0) {
            ESP_LOGW(TAG, "Ignoring unusable legacy custom MAC: %s", custom_mac.c_str());
            return;
        }

        esp_err_t err = esp_base_mac_addr_set(mac);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restore legacy custom MAC %s: %s", custom_mac.c_str(),
                     esp_err_to_name(err));
            return;
        }
        ESP_LOGI(TAG, "Restored legacy custom MAC: %s", custom_mac.c_str());
    }

#ifdef CONFIG_ESP_HI_WEB_CONTROL_ENABLED
    static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data)
    {
        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {

            xTaskCreate(
                [](void* arg) {
                    EspHi* instance = static_cast<EspHi*>(arg);
                    
                    vTaskDelay(5000 / portTICK_PERIOD_MS);

                    if (!instance->web_server_initialized_) {
                        ESP_LOGI(TAG, "WiFi connected, init web control server");
                        esp_err_t err = esp_hi_web_control_server_init();
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "Failed to initialize web control server: %d", err);
                        } else {
                            ESP_LOGI(TAG, "Web control server initialized");
                            instance->web_server_initialized_ = true;
                        }
                    }

                    vTaskDelete(NULL);
                },
                "web_server_init",
                1024 * 10, arg, 5, nullptr);
        }
    }
#endif //CONFIG_ESP_HI_WEB_CONTROL_ENABLED

    void HandleMoveWakePressDown(int64_t current_time, int64_t &last_trigger_time, int &gesture_state)
    {
        int64_t interval = last_trigger_time == 0 ? 0 : current_time - last_trigger_time;
        last_trigger_time = current_time;

        if (interval > 1000) {
            gesture_state = 0;
        } else {
            switch (gesture_state) {
            case 0:
                break;
            case 1:
                if (interval > 300) {
                    gesture_state = 2;
                }
                break;
            case 2:
                if (interval > 100) {
                    gesture_state = 0;
                }
                break;
            }
        }
    }

    void HandleMoveWakePressUp(int64_t current_time, int64_t &last_trigger_time, int &gesture_state)
    {
        int64_t interval = current_time - last_trigger_time;

        if (interval > 1000) {
            gesture_state = 0;
        } else {
            switch (gesture_state) {
            case 0:
                if (interval > 300) {
                    gesture_state = 1;
                }
                break;
            case 1:
                break;
            case 2:
                if (interval < 100) {
                    ESP_LOGI(TAG, "gesture detected");
                    gesture_state = 0;
                    auto &app = Application::GetInstance();
                    app.ToggleChatState();
                }
                break;
            }
        }
    }

    void InitializeButtons()
    {
        static int64_t last_trigger_time = 0;
        static int gesture_state = 0;  // 0: init, 1: wait second long interval, 2: wait oscillation

        boot_button_.OnClick([this]() {
            auto &app = Application::GetInstance();
            // During startup (before connected), pressing BOOT button enters Wi-Fi config mode without reboot
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        audio_wake_button_.OnPressDown([this]() {
        });

        audio_wake_button_.OnPressUp([this]() {
        });

        move_wake_button_.OnPressDown([this]() {
            int64_t current_time = esp_timer_get_time() / 1000;
            HandleMoveWakePressDown(current_time, last_trigger_time, gesture_state);
        });

        move_wake_button_.OnPressUp([this]() {
            int64_t current_time = esp_timer_get_time() / 1000;
            HandleMoveWakePressUp(current_time, last_trigger_time, gesture_state);
        });
    }
    
    void InitializeLed() {
        ESP_LOGI(TAG, "BLINK_GPIO setting %d", bsp_strip_config.strip_gpio_num);

        ESP_ERROR_CHECK(led_strip_new_rmt_device(&bsp_strip_config, &bsp_rmt_config, &led_strip_));
        led_strip_set_pixel(led_strip_, 0, 0x00, 0x00, 0x00);
        led_strip_set_pixel(led_strip_, 1, 0x00, 0x00, 0x00);
        led_strip_set_pixel(led_strip_, 2, 0x00, 0x00, 0x00);
        led_strip_set_pixel(led_strip_, 3, 0x00, 0x00, 0x00);
        led_strip_refresh(led_strip_);
    }

    esp_err_t SetLedColor(uint8_t r, uint8_t g, uint8_t b) {
        esp_err_t ret = ESP_OK;

        ret |= led_strip_set_pixel(led_strip_, 0, r, g, b);
        ret |= led_strip_set_pixel(led_strip_, 1, r, g, b);
        ret |= led_strip_set_pixel(led_strip_, 2, r, g, b);
        ret |= led_strip_set_pixel(led_strip_, 3, r, g, b);
        ret |= led_strip_refresh(led_strip_);
        return ret;
    }

    void InitializeIot()
    {
        ESP_LOGI(TAG, "Initialize Iot");
        InitializeLed();
        SetLedColor(0x00, 0x00, 0x00);

#ifdef CONFIG_ESP_HI_WEB_CONTROL_ENABLED
        esp_event_loop_create_default();
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
                                                 &wifi_event_handler, this));
#endif //CONFIG_ESP_HI_WEB_CONTROL_ENABLED
    }

    void InitializeSpi()
    {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        // Full-frame clear + emoji strips need room for one landscape row band.
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay()
    {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        static const ili9341_vendor_config_t vendor_config = {
            .init_cmds = vendor_specific_init,
            .init_cmds_size = sizeof(vendor_specific_init) / sizeof(ili9341_lcd_init_cmd_t),
        };

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = (void*)&vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        // Software reset (RST is NC) needs extra settle time before init commands.
        vTaskDelay(pdMS_TO_TICKS(120));
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, false);
        esp_lcd_panel_set_gap(panel, 0, 24);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        ESP_LOGI(TAG, "LCD panel create success, %p", panel);

        esp_lcd_panel_disp_on_off(panel, true);

        // Clear residual GRAM so a failed assets load does not leave snow on screen.
        std::vector<uint16_t> black(DISPLAY_WIDTH * DISPLAY_HEIGHT, 0);
        esp_lcd_panel_draw_bitmap(panel, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, black.data());

        ESP_LOGI(TAG, "Create emoji widget, panel: %p, panel_io: %p", panel, panel_io);
        display_ = new anim::EmojiWidget(panel, panel_io);

#if CONFIG_ESP_CONSOLE_NONE
        servo_dog_ctrl_config_t config = {
            .fl_gpio_num = FL_GPIO_NUM,
            .fr_gpio_num = FR_GPIO_NUM,
            .bl_gpio_num = BL_GPIO_NUM,
            .br_gpio_num = BR_GPIO_NUM,
        };

        servo_dog_ctrl_init(&config);
#endif
    }

    void InitializeTools()
    {
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool("self.dog.forward", "讓機器狗使用既有步態向前移動一個週期", PropertyList(),
            [](const PropertyList&) -> ToolResult {
                const DogMotionResult result =
                    DogMotion::GetInstance().Execute(DogMotionAction::kForward);
                if (result != DogMotionResult::kOk) {
                    return std::unexpected(DogMotion::ErrorMessage(result));
                }
                return true;
            });

        Property action_property("action", kPropertyTypeString);
        action_property.SetMaxLength(16);
        mcp_server.AddTool(
            "self.dog.action",
            "執行經白名單限制的機器狗動作。action 只允許 forward、backward、turn_left、turn_right、stop、home。"
            "每次移動最多一個週期；不接受 Servo 角度、速度或重複次數。",
            PropertyList({action_property}), [](const PropertyList& properties) -> ToolResult {
                const std::string& action = properties["action"].value<std::string>();
                const auto parsed_action = DogMotion::Parse(action);
                if (!parsed_action.has_value()) {
                    return std::unexpected("Unsupported dog action: " + action);
                }

                const DogMotionResult result = DogMotion::GetInstance().Execute(*parsed_action);
                if (result != DogMotionResult::kOk) {
                    return std::unexpected(DogMotion::ErrorMessage(result));
                }
                return true;
            });

        // 灯光控制
        mcp_server.AddTool("self.light.get_power", "获取灯是否打开", PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            return led_on_;
        });

        mcp_server.AddTool("self.light.turn_on", "打开灯", PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            SetLedColor(0xFF, 0xFF, 0xFF);
            led_on_ = true;
            return true;
        });

        mcp_server.AddTool("self.light.turn_off", "关闭灯", PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            SetLedColor(0x00, 0x00, 0x00);
            led_on_ = false;
            return true;
        });

        mcp_server.AddTool("self.light.set_rgb", "设置RGB颜色", PropertyList({
            Property("r", kPropertyTypeInteger, 0, 255),
            Property("g", kPropertyTypeInteger, 0, 255),
            Property("b", kPropertyTypeInteger, 0, 255)
        }), [this](const PropertyList& properties) -> ReturnValue {
            int r = properties["r"].value<int>();
            int g = properties["g"].value<int>();
            int b = properties["b"].value<int>();

            led_on_ = true;
            SetLedColor(r, g, b);
            return true;
        });
    }

public:
    EspHi() : boot_button_(BOOT_BUTTON_GPIO),
        audio_wake_button_(AUDIO_WAKE_BUTTON_GPIO),
        move_wake_button_(MOVE_WAKE_BUTTON_GPIO)
    {
        InitializeLegacyDeviceIdentity();
        InitializeButtons();
        InitializeIot();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeTools();
    }

    virtual AudioCodec* GetAudioCodec() override
    {
        static AdcPdmAudioCodec audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_ADC_MIC_CHANNEL,
            AUDIO_PDM_SPEAK_P_GPIO,
            AUDIO_PDM_SPEAK_N_GPIO,
            AUDIO_PA_CTL_GPIO);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override
    {
        return display_;
    }
};

DECLARE_BOARD(EspHi);

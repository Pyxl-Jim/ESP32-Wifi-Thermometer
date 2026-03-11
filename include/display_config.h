#pragma once
#ifdef USE_DISPLAY

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ST7789 172x320 on ESP32-C6 (Waveshare/Wonrabai board)
// SPI pins: MOSI=6, SCLK=7, CS=14, DC=15, RST=21, BL=22
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789  _panel_instance;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance;

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.pin_sclk   = 7;
            cfg.pin_mosi   = 6;
            cfg.pin_miso   = 13;
            cfg.pin_dc     = 15;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs       = 14;
            cfg.pin_rst      = 21;
            cfg.pin_busy     = -1;
            cfg.panel_width  = 172;
            cfg.panel_height = 320;
            cfg.offset_x     = 34;   // ST7789 172-wide panels start at column 34
            cfg.offset_y     = 0;
            cfg.invert       = true;  // required for correct colors on ST7789
            cfg.rgb_order    = false; // BGR
            cfg.readable     = false;
            cfg.bus_shared   = false;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl    = 22;
            cfg.invert    = false;
            cfg.freq      = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }
        setPanel(&_panel_instance);
    }
};

#endif // USE_DISPLAY

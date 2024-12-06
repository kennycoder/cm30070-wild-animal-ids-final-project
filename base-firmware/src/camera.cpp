#include <Arduino.h>
#include "camera.h"
#include "esp32-hal.h"
#include "model_settings.h"

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_XGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
#endif
}

void setupLedFlash(int pin) {
#if CONFIG_LED_ILLUMINATOR_ENABLED
  ledcAttach(pin, 5000, 8);
#else
  log_i("LED flash is disabled -> CONFIG_LED_ILLUMINATOR_ENABLED = 0");
#endif
}

TfLiteStatus GetImage(int image_width, int image_height, int channels, uint8_t* image_data) {
#if ESP_CAMERA_SUPPORTED

  sensor_t *s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_96X96);

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    log_e(TAG, "Camera capture failed");
    return kTfLiteError;
  } else {
    Serial.printf("Image size & length: %d %d %d\n", fb->width, fb->height, fb->len);
  }

  for (int i = 0; i < kNumRows; i++) {
      for (int j = 0; j < kNumCols; j++) {
          uint16_t pixel = ((uint16_t *) (fb->buf))[i * kNumCols + j];

          // Extract RGB components from the 16-bit pixel
          uint8_t hb = pixel & 0xFF;
          uint8_t lb = pixel >> 8;
          uint8_t r = (lb & 0x1F) << 3;
          uint8_t g = ((hb & 0x07) << 5) | ((lb & 0xE0) >> 3);
          uint8_t b = (hb & 0xF8);

          // Store RGB values in the image_data array
          // int index = (i * kNumCols + j) * 3; // Calculate starting index for RGB triplet
          // image_data[index] = r;   // Red channel
          // image_data[index + 1] = g; // Green channel
          // image_data[index + 2] = b; // Blue channel

          // Unfortunately 3 channel model is running poorly on esp32 so needs to be greyscale
          int8_t grey_pixel = ((305 * r + 600 * g + 119 * b) >> 10) - 128;

          image_data[i * kNumCols + j] = grey_pixel;          
      }
  }

  esp_camera_fb_return(fb);
  s->set_framesize(s, FRAMESIZE_XGA); // Return to original resolution

  return kTfLiteOk;
#else
  return kTfLiteError;
#endif
}

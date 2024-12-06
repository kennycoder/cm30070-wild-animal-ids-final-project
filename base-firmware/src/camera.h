#define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM

#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"

#include "esp_camera.h"
#include "camera_pins.h"

void initCamera();
void setupLedFlash(int);
TfLiteStatus GetImage(int image_width, int image_height, int channels, uint8_t* image_data);
void FormatOutputs(uint8_t*m, uint8_t data_channel);
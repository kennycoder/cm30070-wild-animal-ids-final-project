# Wildlife Detection Firmware for ESP32-S3

This project implements firmware for an ESP32-S3 microcontroller with a camera, designed to detect specific wildlife animals (foxes, deer, bears, crocodiles, and wolves) using a custom-trained TensorFlow Lite (TFLite) model. The firmware leverages PlatformIO (or Arduino IDE) for development, compilation, and flashing.

## Overview

The firmware captures images from the attached camera and feeds them to a TFLite model for inference.  The model outputs predictions indicating the presence and likelihood of the target animals in the frame. The results is then used for logging, alerts, or other actions implemented in the gateway software (see `gateway` folder)

## Hardware Requirements

* ESP32-S3 microcontroller with a camera module.
* **Crucially:** A device with at least 8MB of SPI Flash and 8MB of PSRAM (or other suitable external RAM) due to the model size and processing requirements.  The `esp32s3_120_16_8-qio_opi` or similar boards would be appropriate.
* USB connection for flashing and monitoring.


## Software Requirements

* PlatformIO CLI (recommended) or Arduino IDE.
* ESP32 Arduino framework, specifically the Arduino/IDF5 version. This is crucial for compatibility and performance.


## Installation and Setup

1. **Clone this repository:**  `git clone <repository_url>`
2. **Install PlatformIO Core:** If using PlatformIO, follow the instructions on the PlatformIO website to install the CLI.
3. **Open the project in PlatformIO:**  Navigate to the project directory in your terminal and run `pio init`.
4. **Install dependencies:**  PlatformIO will automatically install the required libraries defined in `platformio.ini`.
5. **Configure the `platformio.ini` file:** Ensure the correct board is selected (`esp32s3_120_16_8-qio_opi` or similar with sufficient flash and RAM) and the `platform` is set to the `Arduino/IDF5` fork from the provided URL.  Make any other necessary adjustments (like the monitor speed).
6. **Copy the TFLite Model:** Place your trained `*.tflite` model converted to .cc or .cpp format (use tflite_converter.ipynb) in the `src` directory
7. **Compile and Upload:** Use `pio run -t upload` to compile and flash the firmware to the ESP32 MCU.

**If using Arduino IDE:**

1. Install the ESP32 board support package.
2. Install the required libraries (specified in `platformio.ini` -  PubSubClient, TaskScheduler, ArduinoJson, AsyncTCP, ESP Async WebServer, TensorFlowLite_ESP32).
3. Copy the project files into an Arduino sketch.
4. Copy the TFLite model to the data folder.
5. Select your ESP32-S3 board and configure the settings (partition scheme, etc.)
6. Compile and upload the sketch.

## Configuration

* **Model Path:** Adjust the path within the firmware code to point to your TFLite model file.
* **Camera Configuration:**  Configure camera settings (resolution, frame rate, etc.) as needed within the firmware.
* **Other Settings:**  Modify thresholds for detection confidence, logging behavior, or other application-specific settings within the code.


## Dependencies

The following libraries are used:

* PubSubClient (for MQTT communication if used)
* TaskScheduler (for scheduling tasks)
* ArduinoJson (for JSON handling)
* AsyncTCP (for asynchronous TCP communication)
* ESP Async WebServer (for web server functionality if required)
* TensorFlowLite_ESP32 (for TFLite model inference)

## License

Apache 2.0

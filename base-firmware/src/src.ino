/* This code is a main program for an ESP32 microcontroller. It implements a wearable device that monitors the user's movements and sounds. It uses an accelerometer to detect falls and a microphone to detect noises. 
  The device also has a light sensor to measure ambient light levels. The data is sent to an MQTT broker, where it can be processed by my gateway piece. 
  The device also has a web server that allows users to configure the device settings and view the sensor data. */

/* The code is organized into several functions, each responsible for a specific task. The `setup()` function initializes the device, including the sensors, WiFi connection, and MQTT client. 
  The `loop()` function handles the main loop of the device, which includes reading sensor data, processing the data, and sending the data to the MQTT broker. */

/* The `readInputTask()` function reads the data from the microphone and light sensor. 
  The `sendTelemetry()` function sends the sensor data to the MQTT broker. 
  The `registerDevice()` function registers the device with the MQTT broker. */

/* The `initConfig()` function loads the device settings from the preferences storage. The `saveConfig()` function saves the device settings to the preferences storage. 
  The `initOutputs()` function initializes the output pins. 
  The `initInputs()` function initializes the input pins. 
  The `initWiFi()` function connects to the WiFi network. 
  The `initMQTTClient()` function connects to the MQTT broker. 
  The `handleNotFound()` function handles HTTP requests for non-existent files. 
  Lastly, the `initHTTPServer()` function initializes the web server. The `siren()` function plays a siren sound. */

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include "ESPAsyncWebServer.h"
#include <AsyncTCP.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <TaskScheduler.h>
#include "FS.h"
#include <LittleFS.h>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "model.h"
#include "model_settings.h"

#define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM

#include "esp_camera.h"
#include "camera_pins.h"
#include "camera.h"

#include <regex>
#include <string>

#include "ArduinoJson.h"
#include "esp_adc_cal.h"
#include "esp_sntp.h"
#include <driver/adc.h>

#define DEBUG_ENABLED false
#define MQTT_DEBUG_ENABLED true
#define LED_BUILTIN_PIN 2
#define LED_EXTERNAL_PIN 47
#define BUZZER_PIN 21
#define AUDIO_INPUT_PIN 3
#define PIR_SENSOR_PIN 14
#define DEVICE_TYPE "esp32-s3-node"

#define NOISE_THRESHOLD 250
#define TELEMETRY_INTERVAL 30000
#define DEFAULT_MQTT_PORT 1883

enum class SirenTypes { Animal, Person };

AsyncWebServer server(80);
Scheduler ts;
Preferences preferences;
WiFiClient espClient;
PubSubClient client(espClient);

bool g_NoiseDetected = false;
bool g_Notified = false;
bool g_MovementDetected = false;
uint8_t g_DataChannel = 0;
int g_Loudness = 0;
uint8_t g_PredictedAnimal;
float g_PredictedConfidence;


esp_adc_cal_characteristics_t *adc_chars;

// Function definitions
void initConfig();
void handleNotFound(AsyncWebServerRequest *);
void initHTTPServer();

void mqttCallback(char *, byte *, unsigned int);

// Task function definitions
void readInputTask(void *);
bool analyzeFrameWithTFLiteModel();

// Sensor function definitions
void siren(SirenTypes type);
std::string getSensorDataPayload(std::string event = "default");

// Telemetry task definitions
void sendTelemetry();
Task *sendTelemetryTask;

// Device registration
void registerDevice();
std::string getRegistrationPayload();

// Config struct
typedef struct {
  String ssid;
  String password;
  String gateway;
  int noise_threshold;
  int telemetry_interval;
  String mqtt_broker;
  String mqtt_topic;
  String mqtt_topic_sub;
  String mqtt_username;
  String mqtt_password;
  int mqtt_port;
} Settings;

Settings settings;

// Globals, used for compatibility with Arduino-style sketches.
namespace {
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;

// In order to use optimized tensorflow lite kernels, a signed int8_t quantized
// model is preferred over the legacy unsigned model format. This means that
// throughout this project, input images must be converted from unisgned to
// signed format. The easiest and quickest way to convert from unsigned to
// signed 8-bit integers is to subtract 128 from the unsigned value to get a
// signed value.

// An area of memory to use for input, output, and intermediate arrays.
constexpr int kTensorArenaSize = 136 * 1024;
// Keep aligned to 16 bytes for CMSIS
alignas(16) uint8_t tensor_arena[kTensorArenaSize];
}  // n

// Implementations

void initTensorFlowLite() {
  tflite::InitializeTarget();

  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  model = tflite::GetModel(g_animal_detect_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf(
        "Model provided is schema version %d not equal "
        "to supported version %d.",
        model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }

  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.
  //
  // tflite::AllOpsResolver resolver;
  // NOLINTNEXTLINE(runtime-global-variables)
  static tflite::MicroMutableOpResolver<5> micro_op_resolver;
  micro_op_resolver.AddAveragePool2D();
  micro_op_resolver.AddConv2D();
  micro_op_resolver.AddDepthwiseConv2D();
  micro_op_resolver.AddReshape();
  micro_op_resolver.AddSoftmax();

  
  // Build an interpreter to run the model with.
  // NOLINTNEXTLINE(runtime-global-variables)
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize);

  interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors.
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    MicroPrintf("AllocateTensors() failed");
    return;
  }

  // Get information about the memory area to use for the model's input.
  input = interpreter->input(0);

  if ((input->dims->size != 4) || (input->dims->data[0] != 1) ||
      (input->dims->data[1] != kNumRows) ||
      (input->dims->data[2] != kNumCols) ||
      (input->dims->data[3] != kNumChannels) || (input->type != kTfLiteInt8)) {
    MicroPrintf("Bad input tensor parameters in model");
    return;
  }
}

void initConfig() {
  preferences.begin("settings", false);
  // preferences.clear();
 
  settings.ssid = preferences.getString("ssid", "<WIFI_SSID>"); 
  settings.password = preferences.getString("password", "<WIFI_PASSWORD>");  
  settings.gateway = preferences.getString("gateway", "<WIFI_GATEWAY_IP>");  

  settings.noise_threshold = preferences.getInt("noise_th", NOISE_THRESHOLD);  
  settings.telemetry_interval = preferences.getInt("telemetry_itv", TELEMETRY_INTERVAL);  

  settings.mqtt_broker = preferences.getString("mqtt_broker", "<MQTT_BROKER_IP>"); 
  settings.mqtt_topic = preferences.getString("mqtt_topic", "uol/uol-cm3070-mod11");  
  settings.mqtt_topic_sub = preferences.getString("mqtt_topic_sub", "uol/uol-cm3070-mod11/sub/DE:VI:CE:MA:CA:DR");
  settings.mqtt_username = preferences.getString("mqtt_username", "<MQTT_USERNAME>");  
  settings.mqtt_password = preferences.getString("mqtt_password", "<MQTT_PASSWORD>");  
  settings.mqtt_port = preferences.getInt("mqtt_port", 1883);  
}

void saveConfig(Settings s) {
  preferences.putString("ssid", s.ssid); 
  preferences.putString("password", s.password);  
  preferences.putString("gateway", s.gateway);  

  preferences.putInt("noise_th", s.noise_threshold);  
  preferences.putInt("telemetry_itv", s.telemetry_interval);  

  preferences.putString("mqtt_broker", s.mqtt_broker); 
  preferences.putString("mqtt_topic", s.mqtt_topic);  
  preferences.putString("mqtt_topic_sub", s.mqtt_topic_sub);  
  preferences.putString("mqtt_username", s.mqtt_username);  
  preferences.putString("mqtt_password", s.mqtt_password);  
  preferences.putInt("mqtt_port", s.mqtt_port);  
}

void initOutputs() {
  pinMode(LED_BUILTIN_PIN, OUTPUT);  
  pinMode(LED_EXTERNAL_PIN, OUTPUT);  
  ledcAttach(BUZZER_PIN, 2000, 8);
}

void initInputs() {
  pinMode(PIR_SENSOR_PIN, INPUT_PULLDOWN);
}

void initWiFi() {
  WiFi.begin(settings.ssid, settings.password);

  // Wait for WiFi connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void initMQTTClient() {
    client.setServer(settings.mqtt_broker.c_str(), settings.mqtt_port);
    client.setCallback(mqttCallback);
    while (!client.connected()) {
        String client_id = "danynik-esp32-";
        client_id += String(WiFi.macAddress());
        Serial.printf("The client %s connects to the public MQTT broker\n", client_id.c_str());
        if (client.connect(client_id.c_str(), settings.mqtt_username.c_str(), settings.mqtt_password.c_str())) {
            Serial.println("Public EMQX MQTT broker connected");
        } else {
            Serial.print("failed with state ");
            Serial.print(client.state());
            delay(2000);
        }
    }  

    client.setBufferSize(512); // we have plent of data so increase is needed!
    client.subscribe(settings.mqtt_topic_sub.c_str());    
}

void setup() {  
  Serial.begin(115200);

  if (!LittleFS.begin(true)) {
    Serial.println("FS Mount Failed");
    return;
  } else {
    Serial.println("FS Mounted");
  }

  initConfig();
  Serial.print("Settings loaded.");

  initTensorFlowLite();
  initCamera();
  initOutputs();
  initInputs();

  // Connect to WiFi
  digitalWrite(LED_BUILTIN_PIN, HIGH);
  initWiFi();
  initHTTPServer();
  initMQTTClient();
  digitalWrite(LED_BUILTIN_PIN, LOW);

// Set time from NPT
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  sntp_sync_status_t syncStatus = sntp_get_sync_status();
  Serial.print("Synchronizing time.");
  while (syncStatus != SNTP_SYNC_STATUS_COMPLETED) {
      syncStatus = sntp_get_sync_status();
      Serial.print(".");
      delay(100); // Adjust the delay time as per your requirements
  }
  // sntp_stop();
  
  Serial.print("time synchronized: ");
  Serial.println(time(nullptr));

  // Start the async task because we need constant monitoring without interrupting the CPU
  xTaskCreate ( 
    readInputTask, /* Task function. */
    "readInputTask", /* name of task. */
    1024*4,      /* Stack size of task */
    NULL,           /* parameter of the task */
    1,              /* priority of the task */
    NULL);

  // Set telemetry recurring task
  sendTelemetryTask = new Task( settings.telemetry_interval * TASK_MILLISECOND, -1, &sendTelemetry, &ts, true );  

  registerDevice();
}


// Async task for monitoring ADC1_CHANNEL_0
void readInputTask(void *pvParameters) {
  while (true) {
    // Read data from the microphone (directly from DMA)
    g_Loudness = analogRead(AUDIO_INPUT_PIN);

    if (g_Loudness > settings.noise_threshold) {
      g_NoiseDetected = true;
      //if (DEBUG_ENABLED) {
        Serial.printf("Sample=%d", g_Loudness);
      //}
    } else {
      g_NoiseDetected = false;
    }
    
    // Read data from the microphone (directly from DMA)
    if (digitalRead(PIR_SENSOR_PIN) == HIGH) {
      g_MovementDetected = true;
    } else {
      g_MovementDetected = false;
    }

    vTaskDelay(50);    
  }
}

bool analyzeFrameWithTFLiteModel() {
  // Reset predictions
  g_PredictedAnimal = 0;
  g_PredictedConfidence = 0;

  if (kTfLiteOk != GetImage(kNumCols, kNumRows, kNumChannels, input->data.uint8)) {
    MicroPrintf("Image capture failed.");
  }

  // Run the model on this input and make sure it succeeds.
  if (kTfLiteOk != interpreter->Invoke()) {
    Serial.printf("Invoke failed.");
  }

  TfLiteTensor* output = interpreter->output(0);

  std::map<int, float> modelOutputWeights;

  // Process the inference results.
  modelOutputWeights[kFoxIndex] = ((float)output->data.uint8[kFoxIndex]/255)*100;
  modelOutputWeights[kBearIndex] = ((float)output->data.uint8[kBearIndex]/255)*100;
  modelOutputWeights[kDeerIndex]  = ((float)output->data.uint8[kDeerIndex]/255)*100;
  modelOutputWeights[kCrocodileIndex]  = ((float)output->data.uint8[kCrocodileIndex]/255)*100;
  modelOutputWeights[kWolfIndex]  = ((float)output->data.uint8[kWolfIndex]/255)*100;

  // Initialize variables to store the index and value of the maximum element
  g_PredictedAnimal = 0;
  g_PredictedConfidence = modelOutputWeights[0];

  // Iterate through the map and update the maxIndex and maxValue if necessary
  for (const auto& pair : modelOutputWeights) {
      if (pair.second > g_PredictedConfidence) {
          g_PredictedConfidence = pair.second;
          g_PredictedAnimal = pair.first;
      }
  }

  Serial.printf("Fox score: %s, Bear score: %s, Deer score: %s, Crocodile score: %s, Wolf score: %s\n",
              String(modelOutputWeights[kFoxIndex], 2), 
              String(modelOutputWeights[kBearIndex], 2), 
              String(modelOutputWeights[kDeerIndex], 2),
              String(modelOutputWeights[kCrocodileIndex], 2),
              String(modelOutputWeights[kWolfIndex], 2)
              );


  return modelOutputWeights[kFoxIndex] >= 50
  || modelOutputWeights[kBearIndex] >= 50
  || modelOutputWeights[kDeerIndex]  >= 50
  || modelOutputWeights[kCrocodileIndex] >= 50
  || modelOutputWeights[kWolfIndex] >= 50;
}

void sendTelemetry() {Serial.println("Sending telemetry");
  client.publish(settings.mqtt_topic.c_str(), getSensorDataPayload("telemetry").c_str());
}

void registerDevice() {
  client.publish(settings.mqtt_topic.c_str(), getRegistrationPayload().c_str());
}

void loop() {
  client.loop();

  ts.execute();

  if ((g_MovementDetected || g_NoiseDetected)/* && !g_Notified */) {
    g_Notified = true;
    digitalWrite(LED_EXTERNAL_PIN, HIGH);
    
    if (DEBUG_ENABLED) {
      Serial.printf("movement: %s, noise :%s\n", g_MovementDetected ? "true" : "false", g_NoiseDetected ? "true" : "false");
    }
    
    if (analyzeFrameWithTFLiteModel()) {
      client.publish(settings.mqtt_topic.c_str(), getSensorDataPayload("intrusion").c_str());
    }

  } else {
      g_Notified = false;
      digitalWrite(LED_EXTERNAL_PIN, LOW);
  }

  vTaskDelay(50);
}

//

std::string getSensorDataPayload(std::string event) {
  JsonDocument payload;
  payload["client_id"] = WiFi.macAddress();
  payload["device_type"] = DEVICE_TYPE;
  payload["local_timestamp"] = time(nullptr);
  payload["event"] = event;

  payload["data"]["loudness"] = g_Loudness;
  payload["data"]["noise_detected"] = g_NoiseDetected;
  payload["data"]["movement_detected"] = g_MovementDetected;
  
  if (event == "intrusion") {
    payload["data"]["predicted_animal"] = kCategoryLabels[g_PredictedAnimal];
    payload["data"]["predicted_confidence"] = g_PredictedConfidence;
    payload["data"][kCategoryLabels[g_PredictedAnimal]] = g_PredictedConfidence;
  }

  String buffer;
  serializeJson(payload, buffer);
  return buffer.c_str();

}

std::string getRegistrationPayload() {
  JsonDocument payload;
  payload["client_id"] = WiFi.macAddress();
  payload["device_type"] = DEVICE_TYPE;
  payload["local_timestamp"] = time(nullptr);
  payload["event"] = "registration";
  payload["data"]["ip"] = WiFi.localIP().toString();
  
  String buffer;
  serializeJson(payload, buffer);
  return buffer.c_str();
}

// MQTT callback

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  if (MQTT_DEBUG_ENABLED) {
    Serial.print("Message arrived in topic: ");
    Serial.println(topic);
    Serial.print("Message:");
    for (int i = 0; i < length; i++) {
        Serial.print((char) payload[i]);
    }
    Serial.println();
    Serial.println("-----------------------");    
  }

  JsonDocument doc;
  deserializeJson(doc, payload);

  if(doc["event"] == "fox_alert") {
    // TODO: add more things like flash bright light
    siren(SirenTypes::Animal);    
    ledcWriteTone(BUZZER_PIN, 0);
  } else if(doc["event"] == "bear_alert") {
    // TODO: decide what to do when it's a person
    siren(SirenTypes::Animal);
    ledcWriteTone(BUZZER_PIN, 0);
  } else if(doc["event"] == "deer_alert") {
    // TODO: decide what to do when it's a person
    siren(SirenTypes::Animal);
    ledcWriteTone(BUZZER_PIN, 0);
  } else if(doc["event"] == "crocodile_alert") {
    // TODO: decide what to do when it's a person
    siren(SirenTypes::Animal);
    ledcWriteTone(BUZZER_PIN, 0);
  } else if(doc["event"] == "wolf_alert") {
    // TODO: decide what to do when it's a person
    siren(SirenTypes::Animal);
    ledcWriteTone(BUZZER_PIN, 0);
  }
}


// HTTP Server code

void handleNotFound(AsyncWebServerRequest *request) {
    request->send(404);
}

void streamFile(String path, String mimetype, bool cache, AsyncWebServerRequest *request) {
  if (LittleFS.exists(path)) {
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, path, mimetype);
    if (cache) {
      response->addHeader("Cache-Control", "max-age=60, immutable");
    }
    request->send(response);
  } else {
    Serial.printf("%s not found on local filesystem\n", path);
  }
}

void initHTTPServer() {
  MDNS.addService("http","tcp",80);

  server.onNotFound(handleNotFound);

  // this is triggered every 10? seconds from our main gatewy to see if the device is alive
  server.on("/healthz", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/json", "{\"status\": \"ok\"}");
  });

  server.on("/get-settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;

    doc["ssid"] = preferences.getString("ssid", ""); 
    doc["password"] = preferences.getString("password", "");  
    doc["gateway"] = preferences.getString("gateway", "");

    doc["noise_threshold"] = preferences.getInt("noise_th", NOISE_THRESHOLD);  
    doc["telemetry_interval"] = preferences.getInt("telemetry_itv", TELEMETRY_INTERVAL);  

    doc["mqtt_broker"] = preferences.getString("mqtt_broker", ""); 
    doc["mqtt_topic"] = preferences.getString("mqtt_topic", "");
    doc["mqtt_topic_sub"] = preferences.getString("mqtt_topic_sub", "");  
    doc["mqtt_username"] = preferences.getString("mqtt_username", "");  
    doc["mqtt_password"] = preferences.getString("mqtt_password", "");  
    doc["mqtt_port"] = preferences.getInt("mqtt_port", 1883);  

    String buffer;
    serializeJson(doc, buffer);

    request->send(200, "text/json", buffer);
  });


  server.on("/update-settings", HTTP_POST, [](AsyncWebServerRequest *request) {

    String JSONMessage = request->getParam("settings", true)->value();

    Serial.println(JSONMessage.c_str());
    Serial.println("----");

    JsonDocument doc;
    deserializeJson(doc, JSONMessage.c_str());
    Serial.println("de ser done");

    settings.ssid = doc["ssid"].as<String>();
      Serial.println(settings.ssid);
    settings.password = doc["password"].as<String>();
      Serial.println(settings.password);
    settings.gateway = doc["gateway"].as<String>();
      Serial.println(settings.gateway);

    settings.telemetry_interval = doc["telemetry_interval"].as<int>() > 0 ? doc["telemetry_interval"].as<int>() : TELEMETRY_INTERVAL;
      Serial.println(settings.telemetry_interval);
    settings.noise_threshold = doc["noise_threshold"].as<int>() > 0 ? doc["noise_threshold"].as<int>() : NOISE_THRESHOLD;
      Serial.println(settings.noise_threshold);

    settings.mqtt_broker = doc["mqtt_broker"].as<String>();
      Serial.println(settings.mqtt_broker);
    settings.mqtt_topic = doc["mqtt_topic"].as<String>();
      Serial.println(settings.mqtt_topic);
    settings.mqtt_topic_sub = doc["mqtt_topic_sub"].as<String>();
      Serial.println(settings.mqtt_topic_sub);
    settings.mqtt_username = doc["mqtt_username"].as<String>();
      Serial.println(settings.mqtt_username);
    settings.mqtt_password = doc["mqtt_password"].as<String>();
      Serial.println(settings.mqtt_password);
    settings.mqtt_port = doc["mqtt_port"].as<int>() > 0 ? doc["mqtt_port"].as<int>() : DEFAULT_MQTT_PORT;
      Serial.println(settings.mqtt_port);

    if(DEBUG_ENABLED) {
      Serial.println(settings.ssid);
      Serial.println(settings.password);
      Serial.println(settings.gateway);
      Serial.println(settings.telemetry_interval);
      Serial.println(settings.noise_threshold);
      Serial.println(settings.mqtt_broker);
      Serial.println(settings.mqtt_topic);
      Serial.println(settings.mqtt_topic_sub);
      Serial.println(settings.mqtt_username);
      Serial.println(settings.mqtt_password);
      Serial.println(settings.mqtt_port);
    }

    saveConfig(settings);

    request->send(200, "text/json", "{\"status\": \"settings_updated_device_restarting\"}");
    delay(5000);
    esp_restart();
  });

  server.on("/get-data", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/json", getSensorDataPayload("manual_telemetry").c_str());
  });

  server.on("/capture",  HTTP_GET, [](AsyncWebServerRequest *request) {
    camera_fb_t *fb = NULL;
    fb = esp_camera_fb_get();

    if (!fb) {
      log_e("Camera capture failed");
      return;
    }

    fs::File file = LittleFS.open("/capture.jpg", FILE_WRITE);
    if (!file) {
        printf("Failed to open file for writing\n");
        esp_camera_fb_return(fb);
        return;
    }

    file.write(fb->buf, fb->len);
    file.close();

    streamFile("/capture.jpg", "image/jpeg", false, request);
    
    // // Send the response with headers and image data
    // AsyncWebServerResponse *response = request->beginResponse_P(200, "image/jpeg", fb->buf, fb->len);
    // response->addHeader("Content-Disposition", "inline; filename=capture.jpg"); // Optional header
    // response->addHeader("Cache-Control", "no-cache");                          // Optional: prevents caching
    // request->send(response);

    esp_camera_fb_return(fb);

  });

  server.on("/set-data-channel", HTTP_GET, [](AsyncWebServerRequest *request) {
    g_DataChannel = request->getParam(0)->value().toInt();

    if (g_DataChannel < 0)
      g_DataChannel = 0;

    if (g_DataChannel > 5)
      g_DataChannel = 5;

    AsyncWebServerResponse *response = request->beginResponse(200, "text/json", "{\"status\": \"data_channel_updated\"}");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
  });

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

// Siren sound

void siren(SirenTypes type) {
  switch (type) {
    case SirenTypes::Animal:
      for(int hz = 440; hz < 1000; hz+=25){
        ledcWriteTone(BUZZER_PIN, hz);
        delay(50);
      }
      for(int hz = 1000; hz > 440; hz-=25){
        ledcWriteTone(BUZZER_PIN, hz);
        delay(50);
      }
      break;
    case SirenTypes::Person:
      ledcWriteTone(BUZZER_PIN, 750);
      delay(500);
      break;
    default:
      break;
  }
}

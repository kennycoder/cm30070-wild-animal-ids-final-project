# REWRITE with pyrf24 

from pyrf24 import RF24  # Import from the new library
import OPi.GPIO as GPIO
import paho.mqtt.client as mqtt
import json
import time

# --- nRF24L01 Configuration ---
RADIO_CE_PIN = 25  # Depends on the board, check the docu if you are replicating my orange pi
RADIO_CSN_PIN = 8  # Depends on the board, check the docu if you are replicating my orange pi
RADIO_CHANNEL = 0x76
RADIO_ADDRESSES = {  # Dictionary to store addresses
    "GATEWAY_NODE": b"GATEWAY_NODE",
    "NODE1": b"NODE1",
    "NODE2": b"NODE2",
    "NODE3": b"NODE3"  # My nodes here, should be mac addresses for now
}
# --- MQTT Configuration ---
MQTT_BROKER = "<MQTT_BROKER_IP>"  
MQTT_PORT = 1883  # Default MQTT port
MQTT_TOPIC_SUB = "uol/uol-cm3070-mod11/sub/#"  
# --- Global Variables ---
radio = None
mqtt_client = None
current_client_id = None # Store the device ID of this Orange Pi gateway


def setup_nrf24(device_id):
    global radio, current_device_id
    current_device_id = device_id
    GPIO.setmode(GPIO.BCM)

    radio = RF24(ce_pin=RADIO_CE_PIN, csn_pin=RADIO_CSN_PIN)
    radio.begin() # No parameters needed
    radio.setRetries(15, 15)
    radio.setChannel(RADIO_CHANNEL)
    radio.setDataRate(RF24.BR_1MBPS)
    radio.setPALevel(RF24.PA_HIGH)
    radio.setAutoAck(True)
    radio.enableDynamicPayloads()
    radio.openWritingPipe(RADIO_ADDRESSES[device_id])
    radio.openReadingPipe(1, RADIO_ADDRESSES[device_id])
    radio.startListening()
    print(f"nRF24L01 setup complete for {device_id}.")

def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker with result code " + str(rc))
    client.subscribe(MQTT_TOPIC_SUB) # Subscribe to the main command topic


def on_message(client, userdata, msg):
    try:
        print("Received message from MQTT:", msg.topic, str(msg.payload.decode()))
        data = json.loads(msg.payload.decode())
        target_device = data.get("client_id")
        if target_device and target_device in RADIO_ADDRESSES:
            send_to_nrf24(data, target_device)
        else:
            print("Invalid or missing client_id in MQTT message.")
    except json.JSONDecodeError:
        print("Invalid JSON received from MQTT.")
    except Exception as e:
        print(f"Error processing MQTT message: {e}")


def setup_mqtt():
    global mqtt_client
    mqtt_client = mqtt.Client(client_id=f"rpi_nrf24_mqtt_{current_client_id}") # Include device ID in client ID
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
    mqtt_client.loop_start()


def send_to_nrf24(data, device_id):
    try:
        message = json.dumps(data).encode('utf-8')
        radio.openWritingPipe(RADIO_ADDRESSES[device_id]) # Set correct address before sending!
        if radio.write(message):
            print(f"Sent to nRF24L01 ({device_id}):", data)
        else:
            print(f"Failed to send to nRF24L01 ({device_id}).")
    except Exception as e:
        print(f"Error sending to nRF24L01: {e}")


def receive_from_nrf24(device_id):
    if radio.available():
        length = radio.getDynamicPayloadSize() # Get the dynamic payload size
        received = radio.read(length)
        try:
            data = json.loads(received.decode('utf-8'))
            received_device_id = data.get("device_id")
            if received_device_id:
                print(f"Received from nRF24L01 ({received_device_id}):", data)
                mqtt_client.publish(f"your/mqtt/topic/{received_device_id}", json.dumps(data))
            else:
                print("Missing device_id in received nRF24L01 data.")
        except json.JSONDecodeError:
            print("Invalid JSON received from nRF24L01:", received)
        except Exception as e:
            print(f"Error processing nRF24L01 data: {e}")
        time.sleep(0.01)
        return True
    return False

def main():
    client_id = "GATEWAY_NODE"  # Gateway is running this script
    setup_nrf24(client_id)
    setup_mqtt()

    try:
        while True:
            receive_from_nrf24()
            time.sleep(0.1)
    except KeyboardInterrupt:
        print("Exiting...")
    finally:
        radio.stopListening()
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
        GPIO.cleanup()

if __name__ == "__main__":
    main()
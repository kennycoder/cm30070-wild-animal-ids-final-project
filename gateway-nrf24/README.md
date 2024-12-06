# nRF24L01 MQTT Gateway for Orange Pi

This sub-project acts as a gateway between nRF24L01 radio modules and an MQTT broker. It receives data from nRF24L01 nodes, forwards it to the MQTT broker, and relays messages from the MQTT broker to specific nRF24L01 nodes.

## Requirements

* **Hardware:** Orange Pi (or compatible device) with nRF24L01 transceiver module connected.
* **Software:**
    * Python 3
    * `pyrf24` library: `pip3 install pyrf24`
    * `OPi.GPIO` library: Typically pre-installed on Orange Pi OS images.  If not install it with: `pip3 install OPi.GPIO`
    * `paho-mqtt`:  `pip3 install paho-mqtt`

## Configuration

1. **Hardware Connections:**
    * Connect your nRF24L01 module to the Orange Pi's SPI interface and GPIO pins as defined in the script (`RADIO_CE_PIN` and `RADIO_CSN_PIN`). Adjust these pin numbers if necessary.

2. **Software Configuration:**
    * Open `main.py` and adjust the following:
        * `RADIO_ADDRESSES`: Ensure the addresses match the addresses configured on your nRF24L01 nodes. Use unique byte strings for each node and the gateway.
        * `MQTT_BROKER`: Set the IP address or hostname of your MQTT broker.
        * `MQTT_TOPIC_SUB`: Configure the MQTT topic(s) to subscribe to for commands destined for the nRF24L01 nodes. The current configuration uses a wildcard subscription.
        * `mqtt_client`: In the `setup_mqtt` the client id should be unique


## Running the Gateway

1. **Install Dependencies:**  Make sure you have installed the required Python libraries (see Requirements above).

2. **Navigate to Project Directory:** Open a terminal and navigate to the directory containing `main.py`.

3. **Execute the Script:** Run the script using `python3 main.py`.


## Functionality

* **Receiving from nRF24L01:** The gateway continuously listens for incoming messages from nRF24L01 nodes. Upon receiving a message, it parses the JSON payload, extracts the `device_id`, and publishes the data to the MQTT topic `your/mqtt/topic/{received_device_id}`. **IMPORTANT:** Update this topic to your desired topic.

* **Sending to nRF24L01:** When the gateway receives an MQTT message on the subscribed topic, it parses the JSON payload. If the payload contains a valid `client_id` corresponding to a known nRF24L01 node, the gateway forwards the message to the designated node via the nRF24L01 radio link.

## Stopping the Gateway

Press `Ctrl+C` in the terminal to stop the gateway script.




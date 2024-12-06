/* This code implements a gateway for IoT devices. It uses MQTT to receive data from devices, SQLite to store data, and HTTP to provide a web interface for managing devices and alerts.
The code includes features for client registration, telemetry data storage, rule-based alerts, and user management.
It also provides a web interface for viewing device information, alerts, events, and settings.
*/

// The code uses several structs to represent different data types, such as ClientInfo, ActiveAlerts, Rule.
// Each struct has fields that correspond to the data it represents.
// For example, the ClientInfo struct has fields for the IP address and type of the client device. Other structs have similar mappings to the objects (usually external coming via MQTT) with which the code interacts
// The code also defines methods for working with these structs, such as saveTelemetryData, handleRegistration, and matchRuleAndExecuteCallback.
// These methods allow the code to interact with the database, MQTT broker, and HTTP server.

package main

import (
	"bytes"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"html/template"
	"io"
	"log"
	"net/http"
	"net/url"
	"reflect"
	"regexp"
	"strconv"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	_ "github.com/mattn/go-sqlite3" // Import SQLite driver
)

// ClientInfo struct to hold client data
type ClientInfo struct {
	ID   string `json:"client_id"`
	IP   string `json:"ip"`
	Type string `json:"device_type"`
}

// ActiveAlerts struct to hold active alert data
type ActiveAlerts struct {
	Id        int    `json:"id"`
	Type      string `json:"alert_type"`
	ClientID  string `json:"client_id"`
	Timestamp string `json:"timestamp"`
	Message   string `json:"message"`
}

type Rule struct {
	RuleID        int     `json:"rule_id"`
	ClientID      string  `json:"client_id"`
	ParameterName string  `json:"parameter_name"`
	MinRange      float64 `json:"min_range"`
	MaxRange      float64 `json:"max_range"`
	Trigger       string  `json:"trigger"`
	Callback      string  `json:"callback"`
}

var mqttOptions *mqtt.ClientOptions
var mqttClient mqtt.Client

// Global map to store client data
var clientData = make(map[string]ClientInfo)

var activeAlerts []ActiveAlerts

// StubStorage is a variable of type stubMapping, used to store references to callback functions.
type stubMapping map[string]interface{}

var StubStorage = stubMapping{}

func main() {
	StubStorage = map[string]interface{}{
		"yolo_post_classification": yolo_post_classification,
		"fox_callback":             fox_callback,
		"bear_callback":            bear_callback,
		"wolf_callback":            wolf_callback,
		"deer_callback":            deer_callback,
		"crocodile_callback":       crocodile_callback,
	}

	// Initialize SQLite database
	db, err := sql.Open("sqlite3", "client_data.db")
	if err != nil {
		log.Fatal(err)
	}
	defer db.Close()

	// MQTT client setup
	mqttOptions = mqtt.NewClientOptions()
	mqttOptions.AddBroker("tcp://127.0.0.1:1883")
	mqttOptions.SetUsername("mod11")
	mqttOptions.SetPassword("mod11pwd")
	mqttOptions.SetClientID("go-subscriber")
	mqttOptions.SetDefaultPublishHandler(func(client mqtt.Client, msg mqtt.Message) {
		// This function is called when a message is received on any subscribed topic
		fmt.Printf("Topic: %s\n", msg.Topic())
		fmt.Printf("Message: %s\n", msg.Payload())

		// Parse the MQTT message payload
		var eventPayload map[string]interface{}
		if err := json.Unmarshal(msg.Payload(), &eventPayload); err != nil {
			log.Printf("Error parsing MQTT message: %v\n", err)
			return
		}

		// Determine the event type from the payload
		eventType := eventPayload["event"].(string)

		switch eventType {
		case "telemetry", "intrusion":
			// Save telemetry or fall event data to the database
			if err := saveTelemetryData(db, eventPayload); err != nil {
				log.Printf("Error saving event data: %v\n", err)
			}

			// Marshal the "data" field from eventPayload into JSON format
			dataJSON, err := json.Marshal(eventPayload["data"])
			if err != nil {
				return
			}

			// Match the received data against user-defined rules and execute callbacks if necessary
			err = matchRuleAndExecuteCallback(db, eventPayload["client_id"].(string), dataJSON)
			if err != nil {
				log.Printf("Error: %v", err)
			}

		case "registration":
			// Handle client registration events
			if err := handleRegistration(eventPayload); err != nil {
				log.Printf("Error registering client: %v\n", err)
			}

		default:
			fmt.Printf("Unknown event type: %s.\n", eventType)
		}

	})

	mqttClient = mqtt.NewClient(mqttOptions)
	if token := mqttClient.Connect(); token.Wait() && token.Error() != nil {
		log.Fatal(token.Error())
	}

	if token := mqttClient.Subscribe("uol/uol-cm3070-mod11", 0, nil); token.Wait() && token.Error() != nil {
		log.Fatal(token.Error())
	}

	http.Handle("/static/", http.StripPrefix("/static/", http.FileServer(http.Dir("./static"))))

	// HTTP server setup
	http.HandleFunc("/", func(w http.ResponseWriter, req *http.Request) {
		tmpl := template.Must(template.ParseFiles("templates/index.html"))
		tmpl.Execute(w, nil)
	})
	http.HandleFunc("/users", func(w http.ResponseWriter, req *http.Request) {
		tmpl := template.Must(template.ParseFiles("templates/users.html"))
		tmpl.Execute(w, nil)
	})
	http.HandleFunc("/devices", func(w http.ResponseWriter, req *http.Request) {
		tmpl := template.Must(template.ParseFiles("templates/devices.html"))
		tmpl.Execute(w, nil)
	})
	http.HandleFunc("/events", func(w http.ResponseWriter, req *http.Request) {
		tmpl := template.Must(template.ParseFiles("templates/events.html"))
		tmpl.Execute(w, nil)
	})
	http.HandleFunc("/devices/settings/", func(w http.ResponseWriter, req *http.Request) {
		tmpl := template.Must(template.ParseFiles("templates/settings.html"))

		// Pass the DeviceID to your template
		deviceID, _ := getDeviceId(req, "/devices/settings/")
		log.Println("Device ID - " + deviceID)
		data := struct {
			DeviceID string
		}{
			DeviceID: deviceID,
		}

		tmpl.Execute(w, data)
	})
	http.HandleFunc("/rules", func(w http.ResponseWriter, req *http.Request) {
		tmpl := template.Must(template.ParseFiles("templates/rules.html"))
		tmpl.Execute(w, nil)
	})
	http.HandleFunc("/reminders", func(w http.ResponseWriter, req *http.Request) {
		tmpl := template.Must(template.ParseFiles("templates/reminders.html"))
		tmpl.Execute(w, nil)
	})

	// Api endpoints
	http.HandleFunc("/_devices", func(w http.ResponseWriter, req *http.Request) {
		// return contents of clientData as json
		jsonData, err := json.Marshal(clientData)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.Write(jsonData)
	})

	http.HandleFunc("/_alerts", func(w http.ResponseWriter, req *http.Request) {

		// update activeAlerts and set id value equal to its index in array
		for i := range activeAlerts {
			activeAlerts[i].Id = i
		}

		jsonData, err := json.Marshal(activeAlerts)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.Write(jsonData)
	})

	http.HandleFunc("/_dismiss_alert", func(w http.ResponseWriter, req *http.Request) {
		// get the index from the get parameter
		indexStr := req.URL.Query().Get("index")
		if indexStr == "" {
			http.Error(w, "Missing index parameter", http.StatusBadRequest)
			return
		}

		// convert the index string to an integer
		index, err := strconv.Atoi(indexStr)
		if err != nil {
			http.Error(w, "Invalid index parameter", http.StatusBadRequest)
			return
		}

		// check if the index is valid
		if index < 0 || index >= len(activeAlerts) {
			http.Error(w, "Invalid index parameter", http.StatusBadRequest)
			return
		}

		// remove the alert from the array
		activeAlerts = append(activeAlerts[:index], activeAlerts[index+1:]...)

		// return success
		w.WriteHeader(http.StatusOK)
	})

	// endpoint for /_events that fetches the data from client_events table containing id, client_id, type, local_timestamp, event and data)
	http.HandleFunc("/_events", func(w http.ResponseWriter, req *http.Request) {
		// return contents of clientData as json
		rows, err := db.Query("SELECT id, client_id, type, local_timestamp, event, data FROM events ORDER BY id DESC LIMIT 10")
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		defer rows.Close()

		var events []map[string]interface{}
		for rows.Next() {
			var id int
			var clientID, eventType, event, data string
			var localTimestamp int64
			if err := rows.Scan(&id, &clientID, &eventType, &localTimestamp, &event, &data); err != nil {
				http.Error(w, err.Error(), http.StatusInternalServerError)
				return
			}

			// Convert the data field to a map
			var dataMap map[string]interface{}
			if err := json.Unmarshal([]byte(data), &dataMap); err != nil {
				http.Error(w, err.Error(), http.StatusInternalServerError)
				return
			}

			events = append(events, map[string]interface{}{
				"id":              id,
				"client_id":       clientID,
				"type":            eventType,
				"local_timestamp": time.Unix(localTimestamp, 0).Format("2006-01-02 15:04:05"),
				"event":           event,
				"data":            dataMap,
			})
		}

		jsonData, err := json.Marshal(events)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.Write(jsonData)
	})

	http.HandleFunc("/_rules", func(w http.ResponseWriter, req *http.Request) {
		// return contents of clientData as json
		rows, err := db.Query("SELECT rule_id, client_id, parameter_name, min_range, max_range, trigger, callback FROM rules")
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		defer rows.Close()

		var rules []Rule
		for rows.Next() {
			var rule Rule
			if err := rows.Scan(&rule.RuleID, &rule.ClientID, &rule.ParameterName, &rule.MinRange, &rule.MaxRange, &rule.Trigger, &rule.Callback); err != nil {
				http.Error(w, err.Error(), http.StatusInternalServerError)
				return
			}
			rules = append(rules, rule)
		}

		jsonData, err := json.Marshal(rules)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.Write(jsonData)
	})

	http.HandleFunc("/_devices/settings/", func(w http.ResponseWriter, req *http.Request) {
		// getting and setting the settings
		deviceID, _ := getDeviceId(req, "/_devices/settings/")

		if req.Method == http.MethodGet {
			clientInfo, ok := clientData[deviceID]
			if !ok {
				http.Error(w, "Device not found", http.StatusNotFound)
				return
			}

			// Fetch settings from the device
			resp, err := http.Get(fmt.Sprintf("http://%s/get-settings", clientInfo.IP))
			if err != nil {
				http.Error(w, "Error fetching settings from device", http.StatusInternalServerError)
				return
			}
			defer resp.Body.Close()

			// Passthrough the response
			w.Header().Set("Content-Type", "application/json")
			if _, err := io.Copy(w, resp.Body); err != nil {
				http.Error(w, "Error copying response", http.StatusInternalServerError)
				return
			}
		} else if req.Method == http.MethodPost {
			var settingsData struct {
				DeviceID          string `json:"device_id"`
				SSID              string `json:"ssid"`
				Password          string `json:"password"`
				Gateway           string `json:"gateway"`
				NoiseThreshold    int    `json:"noise_threshold"`
				TelemetryInterval int    `json:"telemetry_interval"`
				MQTTBroker        string `json:"mqtt_broker"`
				MQTTTopic         string `json:"mqtt_topic"`
				MQTTTopicSub      string `json:"mqtt_topic_sub"`
				MQTTUsername      string `json:"mqtt_username"`
				MQTTPassword      string `json:"mqtt_password"`
				MQTTPort          int    `json:"mqtt_port"`
			}

			err := json.NewDecoder(req.Body).Decode(&settingsData)
			if err != nil {
				http.Error(w, "Invalid request payload: "+err.Error(), http.StatusBadRequest)
				return
			}

			settingsData.DeviceID = deviceID

			clientInfo, ok := clientData[settingsData.DeviceID]
			if !ok {
				http.Error(w, "Device not found", http.StatusNotFound)
				return
			}

			settingsPayload, err := json.Marshal(settingsData)
			if err != nil {
				http.Error(w, "Error creating settings payload", http.StatusInternalServerError)
				return
			}

			log.Println(string(settingsPayload))

			// Create a URL encoded form
			data := url.Values{}
			data.Set("settings", string(settingsPayload))

			// Send the settings update request to the device
			resp, err := http.PostForm(fmt.Sprintf("http://%s/update-settings", clientInfo.IP), data)

			if err != nil {
				http.Error(w, "Error updating device settings", http.StatusInternalServerError)
				return
			}
			defer resp.Body.Close()

			// Passthrough the device's response
			w.Header().Set("Content-Type", "application/json")
			if _, err := io.Copy(w, resp.Body); err != nil {
				http.Error(w, "Error copying response", http.StatusInternalServerError)
				return
			}
		} else {
			http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
			return
		}
	})

	go func() {
		log.Fatal(http.ListenAndServe("0.0.0.0:8080", nil))
	}()

	select {} // Keep the program running indefinitely
}

// Function to save event data to the database
func saveTelemetryData(db *sql.DB, eventPayload map[string]interface{}) error {

	clientID := eventPayload["client_id"].(string)
	/*if _, ok := clientData[clientID]; !ok {
		return fmt.Errorf("received data from non registrered client: %s", clientId)
	}*/
	// TODO: remove

	// Extract data from the eventPayload map
	deviceType := eventPayload["device_type"].(string)
	localTimestamp := int64(eventPayload["local_timestamp"].(float64)) // Assuming timestamp is a number

	// Convert "data" field to JSON string
	dataJSON, err := json.Marshal(eventPayload["data"])
	if err != nil {
		return err
	}

	// Insert data into the database
	insertSQL := `
	INSERT INTO events (client_id, type, local_timestamp, event, data)
	VALUES (?, ?, ?, ?, ?);
	`
	_, err = db.Exec(insertSQL, clientID, deviceType, localTimestamp, eventPayload["event"].(string), string(dataJSON))
	return err
}

func handleRegistration(eventPayload map[string]interface{}) error {
	clientID := eventPayload["client_id"].(string)
	deviceType := eventPayload["device_type"].(string)
	deviceIP := eventPayload["data"].(map[string]interface{})["ip"].(string)

	// Store IP and Type in ClientInfo struct
	clientData[clientID] = ClientInfo{
		ID:   clientID,
		IP:   deviceIP,
		Type: deviceType,
	}
	fmt.Printf("Registered client: %s (%s) Type: %s\n", clientID, deviceIP, deviceType)

	return nil
}

func init() {
	go func() {
		for {
			checkClients()
			time.Sleep(30 * time.Second)
		}
	}()
}

func checkClients() {
	for clientID, clientInfo := range clientData {
		// Create a new HTTP client
		client := &http.Client{}

		// Construct the URL for the health check
		url := fmt.Sprintf("http://%s/healthz", clientInfo.IP)

		// Perform the GET request
		resp, err := client.Get(url)
		if err != nil {
			// Log the error and continue to the next client
			log.Printf("Error checking health of client %s: %v\n", clientID, err)
			log.Printf("Removed client %s from the map due to unhealthy status\n", clientID)
			delete(clientData, clientID) // TODO REMOVE
			continue
		}
		defer resp.Body.Close()

		// Check if the response status code is 200 OK
		if resp.StatusCode != http.StatusOK {
			// Remove the client from the map
			delete(clientData, clientID) //TODO REMOVE
			log.Printf("Removed client %s from the map due to unhealthy status\n", clientID)
		} else {
			log.Printf("Client %s is healthy\n", clientID)
		}
	}
}

func matchRuleAndExecuteCallback(db *sql.DB, clientID string, data json.RawMessage) error {
	// Query for rules matching the client ID
	var rows *sql.Rows
	var err error
	rows, err = db.Query("SELECT rule_id, client_id, parameter_name, min_range, max_range, trigger, callback FROM rules")

	if err != nil {
		return err
	}
	defer rows.Close()

	// Iterate over the rules
	for rows.Next() {
		var rule Rule
		err = rows.Scan(&rule.RuleID, &rule.ClientID, &rule.ParameterName, &rule.MinRange, &rule.MaxRange, &rule.Trigger, &rule.Callback)
		if err != nil {
			return err
		}

		if rule.ClientID != "*" && rule.ClientID != clientID {
			continue
		}

		var paramValueMap interface{}
		err = json.Unmarshal(data, &paramValueMap)
		if err != nil {
			return err
		}

		// log.Printf("RULE: %s\n", rule.Trigger)
		// log.Printf("NAME: %s\n", rule.ParameterName)
		// log.Printf("VAL: %v\n", paramValueMap)

		for key, paramValue := range paramValueMap.(map[string]interface{}) {
			if key == rule.ParameterName {
				// Check if the parameter value matches the rule
				switch rule.Trigger {
				case "inside_range_trigger":
					if val, ok := paramValue.(float64); ok && val >= rule.MinRange && val <= rule.MaxRange {
						activeAlerts = append(activeAlerts, ActiveAlerts{Type: "rule trigger", ClientID: clientID, Timestamp: time.Now().Format("2006-01-02 15:04:05"), Message: key + " detected (confidence: " + strconv.FormatFloat(paramValue.(float64), 'f', -1, 64) + ") - alert triggered."})
						go executeCallback(rule.Callback, clientID)
					}
				case "outside_range_trigger":
					if val, ok := paramValue.(float64); ok && (val < rule.MinRange || val > rule.MaxRange) {
						activeAlerts = append(activeAlerts, ActiveAlerts{Type: "rule trigger", ClientID: clientID, Timestamp: time.Now().Format("2006-01-02 15:04:05"), Message: key + " detected (confidence: " + strconv.FormatFloat(paramValue.(float64), 'f', -1, 64) + ") - alert triggered."})
						go executeCallback(rule.Callback, clientID)
					}
				default:
					err = fmt.Errorf("invalid trigger: %s", rule.Trigger)
					return err
				}

			}
		}
	}

	return nil
}

func yolo_post_classification(clientID string) {
	_clientData := clientData[clientID]

	log.Printf("Invoking YOLO model")

	payload := map[string]interface{}{"ip_address": _clientData.IP}
	payloadBytes, err := json.Marshal(payload)
	if err != nil {
		log.Printf("Error marshaling payload: %v", err)
		return
	}

	resp, err := http.Post("http://localhost:8081/infer", "application/json", bytes.NewBuffer(payloadBytes))

	if err != nil {
		log.Printf("Error invoking YOLO model: %v", err)
		return
	}
	defer resp.Body.Close()

	var yoloResponse struct {
		TopLabel   string  `json:"top_label"`
		Confidence float64 `json:"confidence"`
	}

	if err := json.NewDecoder(resp.Body).Decode(&yoloResponse); err != nil {
		log.Printf("Error decoding YOLO response: %v", err)
		return
	}

	log.Printf("### Response from backup YOLO model: %s", yoloResponse.TopLabel)

	switch yoloResponse.TopLabel {
	case "fox":
		go executeCallback("fox_callback")
	case "bear":
		go executeCallback("bear_callback")
	case "wolf":
		go executeCallback("wolf_callback")
	case "timber_wolf":
		go executeCallback("wolf_callback")
	case "crocodile":
		go executeCallback("crocodile_callback")
	case "deer":
		go executeCallback("deer_callback")
	default:
		log.Printf("Unrecognized animal: %s", yoloResponse.TopLabel)
	}
}

func fox_callback(clientID string) {
	log.Printf("Fox detected, doing stuff...")
	mqttClient.Publish(
		"uol/uol-cm3070-mod11/sub/"+clientID,
		0,
		false,
		"{\"event\": \"fox_alert\", \"client_id\": \""+clientID+"\", \"data\": {}} }",
	)

}
func bear_callback(clientID string) {
	log.Printf("Bear detected, doing stuff...")

	mqttClient.Publish(
		"uol/uol-cm3070-mod11/sub/"+clientID,
		0,
		false,
		"{\"event\": \"bear_alert\", \"client_id\": \""+clientID+"\", \"data\": {}} }",
	)
}
func wolf_callback(clientID string) {
	log.Printf("Wolf detected, doing stuff...")

	mqttClient.Publish(
		"uol/uol-cm3070-mod11/sub/"+clientID,
		0,
		false,
		"{\"event\": \"wolf_alert\", \"client_id\": \""+clientID+"\", \"data\": {}} }",
	)
}
func crocodile_callback(clientID string) {
	log.Printf("crocodile detected, publishing to uol/uol-cm3070-mod11-%s\n", clientID)

	mqttClient.Publish(
		"uol/uol-cm3070-mod11/sub/"+clientID,
		0,
		false,
		"{\"event\": \"crocodile_alert\", \"client_id\": \""+clientID+"\", \"data\": {}} }",
	)
}
func deer_callback(clientID string) {
	log.Printf("Deer detected, doing stuff...")

	mqttClient.Publish(
		"uol/uol-cm3070-mod11/sub/"+clientID,
		0,
		false,
		"{\"event\": \"deer_alert\", \"client_id\": \""+clientID+"\", \"data\": {}} }",
	)
}

func executeCallback(funcName string, params ...interface{}) (result interface{}, err error) {
	f := reflect.ValueOf(StubStorage[funcName])
	if len(params) != f.Type().NumIn() {
		err = errors.New("the number of params is out of index")
		return
	}
	in := make([]reflect.Value, len(params))
	for k, param := range params {
		in[k] = reflect.ValueOf(param)
	}
	f.Call(in)
	return
}

func getDeviceId(req *http.Request, path string) (string, error) {
	macRegex := regexp.MustCompile(`^[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}$`)

	// Extract the potential MAC address from the URL path
	deviceID := req.URL.Path[len(path):]
	// remove first character from deviceID

	// Validate the MAC address
	if !macRegex.MatchString(deviceID) {
		return "", fmt.Errorf("invalid device ID: %s", deviceID)
	}

	return deviceID, nil
}

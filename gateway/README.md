# Gateway for Smart Home

This document details the setup and running of the gateway application. It's  the central brain of the whole project where remote devices connect and share data.

## Pre-requisites

```bash
sudo apt-get update
sudo apt-get install mosquitto mosquitto-clients
```

## Setting up mosquitto MQTT broker
In the mosquitto.conf set the following values

```
password_file /etc/mosquitto/passwd
allow_anonymous false
listener 1883 0.0.0.0
```

Make sure the firewall and port forwarding is set correctly.

## Running the application

```bash
go mod tidy
go run .
```

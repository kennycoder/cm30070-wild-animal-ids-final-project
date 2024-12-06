# Running inference.py as a Linux Service on Armbian

This README explains how to run the `inference.py` script as a systemd service on Debian. This will allow the script to start automatically on boot and restart if it crashes.

## Prerequisites

* A Debian-based system with `systemd` installed (we are running Armbian here).
* Python 3 and `pip` installed.
* The `inference.py` script and all its dependencies (Ultralytics, Flask, Pillow, etc.).  See the *Setting Up Dependencies* section below.
* A user account (other than root) that the service will run as. It is strongly advised *against* running services as the root user. I run it under my UOL username.


## Setting Up Dependencies

Create a virtual environment and install the required packages:

```bash
pip install -r requirements.txt  # Install dependencies from a requirements.txt file.
```

## Creating a service

```bash
sudo nano /etc/systemd/system/inference.service
```

```bash
[Unit]
Description=YOLO Object Detection Inference Service
After=network.target

[Service]
User=1000
Group=1000
WorkingDirectory=/home/mod11/inference 
ExecStart=/usr/bin/python3 /home/mod11/inference/inference.py
Restart=always
RestartSec=10  # Restart after 10 seconds if it crashes

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable inference.service
sudo systemctl start inference.service
sudo systemctl status inference.service
```
# Mochoeye

**Mochoeye** is embedded software for the **ESP32-CAM** with **microSD** support, designed for **image capture, local storage, and browser-based management**.  
It turns the device into a lightweight local image gallery accessible over Wi-Fi through a built-in web interface.

## Features

- Capture images on demand
- Save photos directly to **microSD**
- Organize files automatically by date
- Access stored images from a web browser
- Browse folders and files through a simple web UI
- Delete images directly from the browser
- Local HTTP server running on the ESP32-CAM
- Wi-Fi connectivity for remote access on the local network
- NTP time synchronization for timestamp-based filenames

## How it works

When started, Mochoeye:

1. Initializes the ESP32-CAM hardware
2. Connects to the configured Wi-Fi network
3. Synchronizes date and time using NTP
4. Mounts the microSD card
5. Starts an embedded HTTP server
6. Exposes a browser-based interface for image capture and file management

Captured images are stored in JPEG format using timestamp-based names, typically under:

```text
/photos/YYYYMMDD/HHMMSS.jpg

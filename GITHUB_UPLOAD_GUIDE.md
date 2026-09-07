# GitHub Upload Guide

## Suggested repository name

`ESP32-Wireless-PC-Dashboard`

## Suggested GitHub description

Wireless ESP32-S3 PC dashboard with live CPU/RAM/network stats, system health, rotary navigation, and two-way Windows controls over Wi-Fi.

## Suggested topics

`esp32` `esp32-s3` `arduino` `ili9341` `pc-monitor` `pc-dashboard` `python` `psutil` `udp` `windows` `rotary-encoder` `diy-electronics`

## Upload with GitHub Desktop

1. Create a new repository named `ESP32-Wireless-PC-Dashboard`.
2. Copy all files from this folder into the repository folder.
3. Commit with a message such as: `Initial release - wireless PC dashboard`.
4. Publish / push the repository.
5. Add the GitHub description and topics above.

Before publishing, confirm that `firmware/PC_Dashboard_ESP32_S3.ino` contains placeholder Wi-Fi credentials and that `pc/pc_dashboard_sender_control_v3.py` contains `YOUR_ESP32_IP` rather than your private configuration.

#!/bin/sh
set -eu

RULES_FILE="/etc/udev/rules.d/99-reactos-rpi5-flash.rules"
GROUP_NAME="${ROS_FLASH_GROUP:-plugdev}"
STORAGE_VID="${ROS_FLASH_STORAGE_VID:-0781}"
STORAGE_PID="${ROS_FLASH_STORAGE_PID:-5591}"
SERIAL_VID="${ROS_FLASH_SERIAL_VID:-2e8a}"
SERIAL_PID="${ROS_FLASH_SERIAL_PID:-000c}"
RELAY_VID="${ROS_FLASH_RELAY_VID:-5131}"
RELAY_PID="${ROS_FLASH_RELAY_PID:-2007}"

if ! getent group "$GROUP_NAME" >/dev/null; then
    echo "Group '$GROUP_NAME' does not exist" >&2
    exit 1
fi

cat <<RULES | sudo tee "$RULES_FILE" >/dev/null
# ReactOS Raspberry Pi 5 flash/test hardware access.
# Debug Probe serial.
SUBSYSTEM=="tty", ATTRS{idVendor}=="$SERIAL_VID", ATTRS{idProduct}=="$SERIAL_PID", GROUP="$GROUP_NAME", MODE="0660"

# USB HID relay board.
KERNEL=="hidraw*", ATTRS{idVendor}=="$RELAY_VID", ATTRS{idProduct}=="$RELAY_PID", GROUP="$GROUP_NAME", MODE="0660"

# Relay-controlled boot media USB storage bridge.
SUBSYSTEM=="block", KERNEL=="sd?", ATTRS{idVendor}=="$STORAGE_VID", ATTRS{idProduct}=="$STORAGE_PID", GROUP="$GROUP_NAME", MODE="0660"
SUBSYSTEM=="block", KERNEL=="sd?*", ATTRS{idVendor}=="$STORAGE_VID", ATTRS{idProduct}=="$STORAGE_PID", GROUP="$GROUP_NAME", MODE="0660"
RULES

sudo udevadm control --reload-rules
sudo udevadm trigger

echo "Installed $RULES_FILE"
echo "Group: $GROUP_NAME"
echo "Debug Probe: $SERIAL_VID:$SERIAL_PID"
echo "Relay: $RELAY_VID:$RELAY_PID"
echo "Storage: $STORAGE_VID:$STORAGE_PID"
echo "Unplug/replug the devices if permissions do not update immediately."

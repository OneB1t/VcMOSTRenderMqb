#!/bin/sh

# Mount filesystem as R/W
/bin/mount -uw /mnt/app
/bin/mount -uw /mnt/system

# Copy binary from sd card 0 to main unit
echo "Copying VNC binary."
cp /fs/sda0/opengl-render-qnx /navigation/opengl-render-qnx

echo "Modifying startup.sh"
# Name of the file to modify
FILE="/etc/boot/startup.sh"

# Check if the block of code is already present
if ! grep -qF "# QNX VNC CLIENT" "$FILE"; then
  # If not, append it after the specified section
  sed -i '/# DCIVIDEO: Kombi Map/ {
    N
    N
    N
    N
    a \
    # QNX VNC CLIENT \
    if [ -f /navigation/opengl-render-qnx ]; then \
        chmod 0777 /navigation/opengl-render-qnx \
        /navigation/opengl-render-qnx & \
    else \
        echo "File /navigation/opengl-render-qnx does not exist." \
    fi
  }' "$FILE"
else
  echo "Block already present. Replacement skipped."
fi
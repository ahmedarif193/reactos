#!/bin/sh
# One-time privileged setup so QEMU can do USB passthrough of the Edimax
# RTL8822BU (7392:f822) AC1200 dongle and use KVM WITHOUT running qemu as root.
#
# Run ONCE:   sudo sh scripts/setup_qemu_usb_nosudo.sh
# Then log out/in (or `newgrp kvm`) and launch vm_monitor.py as your user.
set -eu

VID=7392
PID=f822
RULE=/etc/udev/rules.d/72-qemu-usb-rtl8822bu.rules

if [ "$(id -u)" -ne 0 ]; then
    echo "Re-run with sudo: sudo sh $0" >&2
    exit 1
fi

USER_NAME="${SUDO_USER:-$(logname 2>/dev/null || echo "$USER")}"
echo "Target user: $USER_NAME"

# 1) KVM group so -enable-kvm works without root
if getent group kvm >/dev/null 2>&1; then
    usermod -aG kvm "$USER_NAME"
    echo "Added $USER_NAME to 'kvm' group."
fi

# 2) udev rule: grant the local user R/W on the dongle (so QEMU usb-host can
#    claim it and auto-detach rtw88_8822bu) and on /dev/kvm.
#    TAG+="uaccess" covers a local seat; GROUP/MODE covers SSH/headless.
cat > "$RULE" <<EOF
SUBSYSTEM=="usb", ATTR{idVendor}=="$VID", ATTR{idProduct}=="$PID", TAG+="uaccess", GROUP="kvm", MODE="0660"
KERNEL=="kvm", GROUP="kvm", MODE="0660"
EOF
echo "Wrote $RULE"

# 3) Free the dongle from the host WiFi driver (rtw88_8822bu). QEMU usb-host
#    running as a non-root user cannot force-detach an in-kernel driver, so the
#    device must be unbound first or the guest only sees Vid_0000&Pid_0000.
HOSTDRV=rtw88_8822bu
DRVDIR=/sys/bus/usb/drivers/$HOSTDRV
if [ -d "$DRVDIR" ]; then
    for intf in "$DRVDIR"/*:*; do
        [ -e "$intf" ] || continue
        kn=$(basename "$intf")
        dev=${kn%:*}
        v=$(cat "/sys/bus/usb/devices/$dev/idVendor" 2>/dev/null || echo)
        p=$(cat "/sys/bus/usb/devices/$dev/idProduct" 2>/dev/null || echo)
        if [ "$v" = "$VID" ] && [ "$p" = "$PID" ]; then
            echo "$kn" > "$DRVDIR/unbind" 2>/dev/null \
                && echo "Unbound $kn from $HOSTDRV (freed for QEMU)."
        fi
    done
fi

# Persist: auto-unbind from rtw88_8822bu whenever the dongle is plugged, so the
# host never re-claims it and QEMU can grab it as your user.
UNBIND_RULE=/etc/udev/rules.d/73-qemu-usb-rtl8822bu-unbind.rules
# Trigger on "bind" (fires AFTER the driver attaches, when DRIVER== is valid).
# "add" fires before binding and never catches the kernel re-claiming the
# device when QEMU releases it on VM exit -- which is why it kept coming back.
cat > "$UNBIND_RULE" <<EOF
ACTION=="bind", SUBSYSTEM=="usb", DRIVER=="$HOSTDRV", ATTRS{idVendor}=="$VID", ATTRS{idProduct}=="$PID", RUN+="/bin/sh -c 'echo %k > /sys/bus/usb/drivers/$HOSTDRV/unbind'"
EOF
echo "Wrote $UNBIND_RULE"

# 4) reload rules and reapply to the already-plugged device
udevadm control --reload-rules
udevadm trigger --attr-match=idVendor="$VID" --attr-match=idProduct="$PID"
udevadm trigger /dev/kvm 2>/dev/null || true

echo
echo "Done. Now: log out/in (or run 'newgrp kvm'), replug the dongle if needed,"
echo "then launch QEMU as your normal user — no sudo required."

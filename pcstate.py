#!/usr/bin/env python3
"""Usage: pcstate.py on|off  -- tells the ESP32 the PC's power state over USB."""
import glob, sys, time
import serial   # apt install python3-serial

PATTERN = "/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*" # hard coded not ok

def send(msg, retry_seconds):
    deadline = time.time() + retry_seconds
    while True:
        ports = glob.glob(PATTERN)
        if ports:
            try:
                s = serial.Serial()
                s.port = ports[0]
                s.baudrate = 115200
                s.dtr = False      # don't reset the ESP32 when opening
                s.rts = False
                s.write_timeout = 1
                s.open()
                for _ in range(3):             # repeat for reliability
                    s.write((msg + "\n").encode())
                    s.flush()
                    time.sleep(0.1)
                s.close()
                return True
            except (serial.SerialException, OSError):
                pass
        if time.time() >= deadline:
            return False
        time.sleep(1)

if __name__ == "__main__":
    if len(sys.argv) != 2 or sys.argv[1] not in ("on", "off"):
        sys.exit("usage: pcstate.py on|off")
    msg = sys.argv[1]
    # "on": the USB port may still be enumerating after boot/resume, so retry.
    # "off": we're going down right now, only try briefly.
    send(msg, 20 if msg == "on" else 2)

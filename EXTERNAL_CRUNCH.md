# External Crunch companion

The companion provides Crunch-FZ's second operating mode. Genuine upstream Crunch runs on a Raspberry Pi or other Linux computer; the Flipper Zero supplies the bounded configuration, start/cancel control, and measured status display. Generated wordlists remain on Linux under `/var/lib/crunch-fz/output`.

## Hardware

- Raspberry Pi or another Linux computer
- Three female-to-female jumper wires for a Pi, or a USB-to-3.3 V UART adapter for a laptop
- Flipper Zero with Crunch-FZ 1.0.4

Both sides must use 3.3 V UART. Do not connect a 5 V UART signal and do not connect either device's power pin to the other. For a Raspberry Pi 40-pin header, connect:

| Flipper Zero | Raspberry Pi |
|---|---|
| Pin 13, USART TX | Physical pin 10, GPIO15/RX |
| Pin 14, USART RX | Physical pin 8, GPIO14/TX |
| Pin 11, GND | Physical pin 6, GND |

TX and RX are crossed. Crunch-FZ temporarily disables the firmware expansion listener while its external screen owns USART and restores it on exit.

A Linux laptop can run the same bridge through a USB-to-3.3 V UART adapter. A virtual machine requires the UART adapter to be passed through to the Linux guest.

## Prepare Raspberry Pi OS/Linux

On Raspberry Pi OS, enable UART hardware and disable the serial login console:

```sh
sudo raspi-config
```

Choose **Interface Options → Serial Port**, answer **No** to the login shell and **Yes** to serial hardware, then reboot.

Install genuine Crunch and Python support:

```sh
sudo apt update
sudo apt install crunch python3-venv
```

Copy this repository's `companion` directory to `/opt/crunch-fz`, then prepare it:

```sh
cd /opt/crunch-fz
python3 -m venv venv
./venv/bin/pip install -r requirements.txt
sudo mkdir -p /var/lib/crunch-fz/output
```

Confirm the real executable before connecting the Flipper:

```sh
command -v crunch
crunch -V
```

## Run and test

With UART connected, run:

```sh
sudo /opt/crunch-fz/venv/bin/python /opt/crunch-fz/crunch_fz_bridge.py --serial /dev/serial0 --baud 115200
```

On the Flipper:

1. Configure charset, minimum/maximum length, optional pattern/literal mask, and output filename.
2. Set **Charset and lengths → External baud → 115200**.
3. Open **External Crunch**.
4. Wait for the actual Crunch version and `IDLE`.
5. Press OK to generate. Press OK again to cancel.
6. Press Back to stop any running generation and release UART.

The external screen reports actual newline count, file bytes, monotonic elapsed time, output filename, process state, and Crunch version. Filesystem failures and the final bounded diagnostic from genuine Crunch are returned to the Flipper as protocol errors instead of being discarded. Retrieve generated files from:

```text
/var/lib/crunch-fz/output
```

An existing filename is never silently replaced. The bridge selects a numbered name such as `wordlist-1.txt`.

## Automatic startup

Install `crunch-fz-bridge.service.example` as `/etc/systemd/system/crunch-fz-bridge.service`, verify its paths and UART device, then run:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now crunch-fz-bridge.service
sudo systemctl status crunch-fz-bridge.service
```

## Security and full CLI access

The bridge accepts only fixed CWF1 commands and never passes UART input to a shell. It validates lengths, modes, printable fields, patterns, literal masks, and filenames before constructing the genuine Crunch argument vector.

Options not represented by the Flipper interface—such as permutations, duplicate-run limits, split files, compression, start/end blocks, resume, inversion, Unicode sets, and pipelines—remain available directly through the normal Crunch command line on Linux.

## Validation status

The C protocol parser, UART lifecycle, fixed command construction, output collision handling, measured counters, companion tests, and firmware build can be validated without a Pi. The complete UART/process workflow is not marked hardware-validated until it is observed with physical Raspberry Pi/Linux UART hardware.

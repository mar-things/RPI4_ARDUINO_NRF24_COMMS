#!/usr/bin/env python3
import argparse
import signal
import struct
import time

import RPi.GPIO as GPIO
import spidev

from lib_nrf24 import NRF24


BBB_ADDRESS = [0xE7, 0x1C, 0xE3]
BBB_REVERSED_ADDRESS = [0xE3, 0x1C, 0xE7]
EXAMPLE_ADDRESS = [0xC2, 0xC2, 0xC2, 0xC2, 0xC2]
DEFAULT_ADDRESS = EXAMPLE_ADDRESS
RAW_ARDUINO_ADDRESS = [ord(character) for character in "00001"]
DISPLAY_ADDRESS = "C2 C2 C2 C2 C2"
DEFAULT_REPLY_ADDRESS = [0xE7, 0xE7, 0xE7, 0xE7, 0xE7]
TEMPERATURE_PAYLOAD_SIZE = 4
MAX_PAYLOAD_SIZE = 32
BAD_SPI_STATUSES = {0x00, 0xFF}
FIFO_RX_EMPTY = 0x01


def parse_address(value):
    presets = {
        "default": DEFAULT_ADDRESS,
        "bbb": BBB_ADDRESS,
        "bbb-reversed": BBB_REVERSED_ADDRESS,
        "arduino": DEFAULT_ADDRESS,
        "arduino-raw": RAW_ARDUINO_ADDRESS,
        "c2": EXAMPLE_ADDRESS,
        "e7": DEFAULT_REPLY_ADDRESS,
        "00001": [ord(character) for character in "00001"],
        "10000": [ord(character) for character in "10000"],
    }

    key = value.strip().lower()
    if key in presets:
        return list(presets[key])

    cleaned = key.replace("0x", "").replace(",", " ").replace(":", " ")
    parts = cleaned.split()
    if len(parts) in (3, 5):
        try:
            return [int(part, 16) for part in parts]
        except ValueError as exc:
            raise argparse.ArgumentTypeError(
                "address bytes must be hexadecimal, for example: e7 1c e3"
            ) from exc

    compact = cleaned.replace(" ", "")
    if len(compact) in (6, 10):
        try:
            return [
                int(compact[index : index + 2], 16)
                for index in range(0, len(compact), 2)
            ]
        except ValueError as exc:
            raise argparse.ArgumentTypeError(
                "address must be 3 or 5 hex bytes, for example: e71ce3"
            ) from exc

    if len(value) in (3, 5):
        return [ord(character) for character in value]

    raise argparse.ArgumentTypeError(
        "address must be a preset, 3 or 5 ASCII characters, or 3 or 5 hex bytes"
    )


def format_address(address):
    printable = "".join(chr(byte) if 32 <= byte <= 126 else "." for byte in address)
    hex_bytes = " ".join(f"{byte:02X}" for byte in address)
    return f"{hex_bytes} ({printable})"


def decode_payload(payload):
    raw = " ".join(f"{byte:02X}" for byte in payload)

    if len(payload) >= TEMPERATURE_PAYLOAD_SIZE:
        temp_c_x100, temp_f_x100 = struct.unpack("<hh", bytes(payload[:4]))
        temperature_c = temp_c_x100 / 100.0
        temperature_f = temp_f_x100 / 100.0

        if -55.0 <= temperature_c <= 125.0:
            return (
                f"Temperature in C : {temperature_c:.2f} C   "
                f"Temperature in F : {temperature_f:.2f} F   Raw: {raw}"
            )

    text = bytes(payload).rstrip(b"\x00").decode("utf-8", errors="replace")
    if text:
        return f"Received text: {text}   Raw: {raw}"

    return f"Received raw: {raw}"


def build_parser():
    parser = argparse.ArgumentParser(
        description="Receive NRF24L01 temperature or text packets."
    )
    parser.add_argument(
        "--ce",
        type=int,
        default=17,
        help="GPIO pin connected to NRF24L01 CE. Default: 17",
    )
    parser.add_argument(
        "--csn",
        type=int,
        default=0,
        help="SPI chip select device. 0 = CE0, 1 = CE1. Default: 0",
    )
    parser.add_argument(
        "--address",
        type=parse_address,
        default=DEFAULT_ADDRESS,
        help=(
            "3 or 5-byte receive address. Use default/bbb, bbb-reversed, "
            "arduino-raw, e7, c2, ASCII chars, or hex like e71ce3. "
            "Default: C2 C2 C2 C2 C2"
        ),
    )
    parser.add_argument(
        "--pipe",
        type=int,
        default=0,
        choices=range(0, 6),
        metavar="0-5",
        help="NRF24 reading pipe. Default: 0",
    )
    parser.add_argument(
        "--dynamic-payload",
        action="store_true",
        help="Use dynamic payloads instead of the Arduino sketch's fixed 4-byte payload.",
    )
    parser.add_argument(
        "--pa-level",
        choices=("min", "low", "high", "max"),
        default="min",
        help="Radio power level. Default: min",
    )
    parser.add_argument(
        "--no-details",
        action="store_true",
        help="Do not print NRF24 register details on startup.",
    )
    return parser


def main():
    args = build_parser().parse_args()

    GPIO.setmode(GPIO.BCM)

    pa_levels = {
        "min": NRF24.PA_MIN,
        "low": NRF24.PA_LOW,
        "high": NRF24.PA_HIGH,
        "max": NRF24.PA_MAX,
    }

    radio = NRF24(GPIO, spidev.SpiDev())
    radio.begin(args.csn, args.ce)
    radio.write_register(NRF24.SETUP_AW, len(args.address) - 2)
    radio.setRetries(5, 15)
    radio.setChannel(0x60)
    radio.setDataRate(NRF24.BR_1MBPS)
    radio.setPALevel(pa_levels[args.pa_level])
    radio.setAutoAck(True)

    if args.dynamic_payload:
        radio.setPayloadSize(MAX_PAYLOAD_SIZE)
        radio.enableDynamicPayloads()
        radio.enableAckPayload()
    else:
        radio.setPayloadSize(TEMPERATURE_PAYLOAD_SIZE)

    radio.openReadingPipe(args.pipe, args.address)
    radio.flush_rx()
    radio.startListening()

    if not args.no_details:
        radio.printDetails()

    running = True

    def stop(_signum, _frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    print("Raspberry Pi NRF24 receiver started...")
    address_description = format_address(args.address)
    if args.address == DEFAULT_ADDRESS:
        address_description = (
            f"{DISPLAY_ADDRESS} "
            f"(lib_nrf24 input {format_address(args.address)})"
        )

    print(
        f"CE GPIO: {args.ce}, CSN: SPI CE{args.csn}, "
        f"pipe: {args.pipe}, address: {address_description}, "
        f"payloads: {'dynamic' if args.dynamic_payload else 'fixed 4-byte'}"
    )

    last_spi_warning = 0

    try:
        while running:
            status = radio.get_status()
            if status in BAD_SPI_STATUSES:
                now = time.time()
                if now - last_spi_warning >= 2:
                    print(
                        f"NRF24 SPI/status looks invalid: 0x{status:02X}. "
                        "Check wiring, 3.3V power, SPI enable, CE/CSN pins."
                    )
                    last_spi_warning = now
                time.sleep(0.1)
                continue

            if radio.read_register(NRF24.FIFO_STATUS) & FIFO_RX_EMPTY:
                time.sleep(0.01)
                continue

            pipe = [0]
            if radio.available(pipe):
                while radio.available(pipe):
                    if args.dynamic_payload:
                        payload_size = radio.getDynamicPayloadSize()
                        if payload_size < 1 or payload_size > MAX_PAYLOAD_SIZE:
                            print(f"Ignored invalid dynamic payload size: {payload_size}")
                            radio.flush_rx()
                            break
                    else:
                        payload_size = TEMPERATURE_PAYLOAD_SIZE

                    payload = []
                    radio.read(payload, payload_size)

                    if payload == [0xFF] * len(payload):
                        print("Ignored invalid empty/noise packet: FF bytes")
                        radio.flush_rx()
                        continue

                    if payload == [0x00] * len(payload):
                        radio.flush_rx()
                        time.sleep(0.01)
                        continue

                    print(decode_payload(payload))
            else:
                time.sleep(0.01)
    finally:
        radio.stopListening()
        GPIO.cleanup()
        if getattr(radio, "spidev", None):
            radio.end()

    print("Receiver stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/python3
# -*- coding: utf-8 -*-

import RPi.GPIO as GPIO
import spidev
from lib_nrf24 import NRF24
import time
import struct

GPIO.setmode(GPIO.BCM)

pipes = [
    [0xe7, 0xe7, 0xe7, 0xe7, 0xe7],
    [0xc2, 0xc2, 0xc2, 0xc2, 0xc2]
]

radio = NRF24(GPIO, spidev.SpiDev())
radio.begin(0, 17)

radio.setRetries(15, 15)

radio.setPayloadSize(32)
radio.setChannel(0x60)
radio.setDataRate(NRF24.BR_2MBPS)
radio.setPALevel(NRF24.PA_MIN)

radio.setAutoAck(True)
radio.enableDynamicPayloads()
radio.enableAckPayload()

radio.openWritingPipe(pipes[0])
radio.openReadingPipe(1, pipes[1])

radio.startListening()
radio.stopListening()
radio.printDetails()
radio.startListening()

print("Raspberry Pi NRF24 temperature receiver started...")

while True:
    pipe = [0]

    while not radio.available(pipe):
        time.sleep(0.01)

    recv_buffer = []
    payload_size = radio.getDynamicPayloadSize()
    radio.read(recv_buffer, payload_size)

    print("Raw received:", recv_buffer)

    if len(recv_buffer) >= 4:
        raw_bytes = bytes(recv_buffer[:4])
        tempC_x100, tempF_x100 = struct.unpack("<hh", raw_bytes)

        print("Temperature in C : {:.2f} °C   Temperature in F : {:.2f} °F"
              .format(tempC_x100 / 100.0, tempF_x100 / 100.0))
    else:
        print("Incomplete payload")
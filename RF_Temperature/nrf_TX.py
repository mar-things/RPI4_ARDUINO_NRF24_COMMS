from RF24 import *
from RF24Network import *

# Initialize radio
radio = RF24(22, 0) # CE=22, CSN=0 (GPIO8)
radio.begin()
radio.setPALevel(RF24_PA_LOW)
radio.openWritingPipe(0xF0F0F0F0E1LL)
radio.stopListening()

# Send data
msg = "Hello World"
radio.write(msg.encode())

import smbus
import time

bus = smbus.SMBus(1)
address = 0x48

def read_temp():

    data = bus.read_i2c_block_data(address, 0x00, 2)

    temp = data[0] << 8 | data[1] >> 5
    if temp > 1023:
        temp -= 2048
    return temp * 0.125

while True:
    print(f"Temperature: {read_temp()} C")
    time.sleep(1)
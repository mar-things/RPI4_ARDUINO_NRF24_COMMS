#include <SPI.h>
#include <Wire.h>
#include <nRF24L01.h>
#include <RF24.h>

RF24 radio(7, 8); // CE, CSN

const byte address[5] = {0xC2, 0xC2, 0xC2, 0xC2, 0xC2};
const byte lm75Address = 0x48;

struct TemperatureData {
  int16_t tempC_x100;
  int16_t tempF_x100;
};

TemperatureData data;

bool readLM75Temperature(TemperatureData &temperature) {
  Wire.beginTransmission(lm75Address);
  Wire.write(0x00); // Temperature register
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(lm75Address, (byte)2) != 2) {
    return false;
  }

  int16_t raw = (Wire.read() << 8) | Wire.read();
  raw >>= 5; // LM75B/LM75BD temperature is an 11-bit signed value.

  long tempC_x100 = raw * 125L;
  if (tempC_x100 >= 0) {
    tempC_x100 += 5;
  } else {
    tempC_x100 -= 5;
  }

  temperature.tempC_x100 = tempC_x100 / 10;
  temperature.tempF_x100 = (temperature.tempC_x100 * 9L / 5L) + 3200;

  return true;
}

void setup() {
  Serial.begin(115200);
  Wire.begin();

  bool radioStarted = radio.begin();
  Serial.print("radio.begin(): ");
  Serial.println(radioStarted ? "OK" : "FAILED");

  Serial.print("radio.isChipConnected(): ");
  Serial.println(radio.isChipConnected() ? "YES" : "NO");

  radio.setAddressWidth(5);
  radio.setRetries(5, 15);
  radio.setChannel(0x60);
  radio.setDataRate(RF24_1MBPS);
  radio.setPALevel(RF24_PA_LOW);

  radio.setAutoAck(true);
  radio.setPayloadSize(sizeof(data));

  radio.openWritingPipe(address);
  radio.stopListening();

  Serial.println("TX started");
  Serial.println("NRF24 address: C2 C2 C2 C2 C2, channel: 0x60, data rate: 1 Mbps, Auto-ACK: on");
  Serial.print("Payload size: ");
  Serial.println(sizeof(data));
}

void loop() {
  if (!readLM75Temperature(data)) {
    Serial.println("LM75 read failed");
    delay(1000);
    return;
  }

  bool sent = radio.write(&data, sizeof(data));

  if (sent) {
    Serial.print("Sent temperature: ");
    Serial.print(data.tempC_x100 / 100.0, 2);
    Serial.print(" C / ");
    Serial.print(data.tempF_x100 / 100.0, 2);
    Serial.println(" F");
  } else {
    Serial.println("Send failed");
  }

  delay(500);
}

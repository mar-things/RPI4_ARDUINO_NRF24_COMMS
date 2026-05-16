#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>

namespace {

constexpr uint8_t ADDRESS[5] = {0xC2, 0xC2, 0xC2, 0xC2, 0xC2};
constexpr uint8_t PAYLOAD_SIZE = 4;

constexpr uint8_t R_REGISTER = 0x00;
constexpr uint8_t W_REGISTER = 0x20;
constexpr uint8_t R_RX_PAYLOAD = 0x61;
constexpr uint8_t FLUSH_RX = 0xE2;
constexpr uint8_t FLUSH_TX = 0xE1;
constexpr uint8_t NOP = 0xFF;

constexpr uint8_t CONFIG = 0x00;
constexpr uint8_t EN_AA = 0x01;
constexpr uint8_t EN_RXADDR = 0x02;
constexpr uint8_t SETUP_AW = 0x03;
constexpr uint8_t SETUP_RETR = 0x04;
constexpr uint8_t RF_CH = 0x05;
constexpr uint8_t RF_SETUP = 0x06;
constexpr uint8_t STATUS = 0x07;
constexpr uint8_t RX_ADDR_P0 = 0x0A;
constexpr uint8_t RX_PW_P0 = 0x11;
constexpr uint8_t FIFO_STATUS = 0x17;
constexpr uint8_t DYNPD = 0x1C;
constexpr uint8_t FEATURE = 0x1D;

constexpr uint8_t MASK_RX_DR = 0x40;
constexpr uint8_t MASK_TX_DS = 0x20;
constexpr uint8_t MASK_MAX_RT = 0x10;
constexpr uint8_t RX_EMPTY = 0x01;

volatile std::sig_atomic_t running = 1;

struct TemperatureData {
  int16_t tempC_x100;
  int16_t tempF_x100;
};

void stopReceiver(int) {
  running = 0;
}

class GpioOut {
 public:
  explicit GpioOut(int pin) : pin_(pin) {}

  void set(bool high) {
    const std::string command =
        "pinctrl set " + std::to_string(pin_) + (high ? " op dh" : " op dl");
    if (std::system(command.c_str()) != 0) {
      throw std::runtime_error("Unable to set GPIO" + std::to_string(pin_) + " with pinctrl");
    }
  }

 private:
  int pin_;
};

class SpiDevice {
 public:
  explicit SpiDevice(const std::string &device, uint32_t speedHz = 100000) : speedHz_(speedHz) {
    fd_ = open(device.c_str(), O_RDWR);
    if (fd_ < 0) {
      throw std::runtime_error("Unable to open " + device + ": " + std::strerror(errno));
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint8_t lsbFirst = 0;
    if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(fd_, SPI_IOC_RD_MODE, &mode) < 0 ||
        ioctl(fd_, SPI_IOC_WR_LSB_FIRST, &lsbFirst) < 0 ||
        ioctl(fd_, SPI_IOC_RD_LSB_FIRST, &lsbFirst) < 0 ||
        ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd_, SPI_IOC_RD_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speedHz_) < 0 ||
        ioctl(fd_, SPI_IOC_RD_MAX_SPEED_HZ, &speedHz_) < 0) {
      throw std::runtime_error("Unable to configure SPI: " + std::string(std::strerror(errno)));
    }
  }

  ~SpiDevice() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  uint8_t transfer(uint8_t byte) {
    uint8_t rx = 0;
    transferBuffer(&byte, &rx, 1);
    return rx;
  }

  void transferBuffer(const uint8_t *tx, uint8_t *rx, uint32_t len) {
    spi_ioc_transfer tr{};
    tr.tx_buf = reinterpret_cast<uintptr_t>(tx);
    tr.rx_buf = reinterpret_cast<uintptr_t>(rx);
    tr.len = len;
    tr.speed_hz = speedHz_;
    tr.bits_per_word = 8;

    if (ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) < 0) {
      throw std::runtime_error("SPI transfer failed: " + std::string(std::strerror(errno)));
    }
  }

 private:
  int fd_ = -1;
  uint32_t speedHz_;
};

class Nrf24 {
 public:
  explicit Nrf24(SpiDevice &spi) : spi_(spi) {}

  uint8_t command(uint8_t cmd) {
    return spi_.transfer(cmd);
  }

  uint8_t readReg(uint8_t reg) {
    uint8_t tx[2] = {static_cast<uint8_t>(R_REGISTER | (reg & 0x1F)), NOP};
    uint8_t rx[2] = {};
    spi_.transferBuffer(tx, rx, sizeof(tx));
    return rx[1];
  }

  void writeReg(uint8_t reg, uint8_t value) {
    uint8_t tx[2] = {static_cast<uint8_t>(W_REGISTER | (reg & 0x1F)), value};
    uint8_t rx[2] = {};
    spi_.transferBuffer(tx, rx, sizeof(tx));
  }

  void writeRegBytes(uint8_t reg, const uint8_t *data, uint8_t len) {
    uint8_t tx[6] = {};
    uint8_t rx[6] = {};
    tx[0] = static_cast<uint8_t>(W_REGISTER | (reg & 0x1F));
    for (uint8_t i = 0; i < len; ++i) {
      tx[i + 1] = data[i];
    }
    spi_.transferBuffer(tx, rx, len + 1);
  }

  void readPayload(uint8_t *data, uint8_t len) {
    uint8_t tx[33] = {};
    uint8_t rx[33] = {};
    tx[0] = R_RX_PAYLOAD;
    for (uint8_t i = 1; i <= len; ++i) {
      tx[i] = NOP;
    }
    spi_.transferBuffer(tx, rx, len + 1);
    std::memcpy(data, rx + 1, len);
  }

 private:
  SpiDevice &spi_;
};

std::string hexByte(uint8_t value) {
  std::ostringstream out;
  out << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(value);
  return out.str();
}

void printUsage(const char *programName) {
  std::cout << "Usage: " << programName << " [--ce GPIO] [--csn SPI_CE]\n"
            << "  --ce   GPIO pin connected to NRF24L01 CE. Default: 17\n"
            << "  --csn  SPI chip select device. 0 = CE0, 1 = CE1. Default: 0\n";
}

bool parseArgs(int argc, char **argv, int &cePin, int &csnDevice) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--ce") == 0 && i + 1 < argc) {
      cePin = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--csn") == 0 && i + 1 < argc) {
      csnDevice = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--help") == 0) {
      printUsage(argv[0]);
      return false;
    } else {
      std::cerr << "Unknown or incomplete argument: " << argv[i] << "\n";
      printUsage(argv[0]);
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  int cePin = 17;
  int csnDevice = 0;

  if (!parseArgs(argc, argv, cePin, csnDevice)) {
    return argc > 1 && std::strcmp(argv[1], "--help") == 0 ? 0 : 1;
  }

  try {
    const std::string spiPath = "/dev/spidev0." + std::to_string(csnDevice);
    GpioOut ce(cePin);
    ce.set(false);

    SpiDevice spi(spiPath, 100000);
    Nrf24 radio(spi);

    const uint8_t initialStatus = radio.command(NOP);
    if (initialStatus == 0x00 || initialStatus == 0xFF) {
      std::cerr << "NRF24 SPI status invalid: " << hexByte(initialStatus) << "\n";
      return 1;
    }

    radio.writeReg(CONFIG, 0x0C);  // CRC on, 2-byte CRC, power down while configuring.
    radio.writeReg(EN_AA, 0x01);   // Auto-ACK pipe 0.
    radio.writeReg(EN_RXADDR, 0x01);
    radio.writeReg(SETUP_AW, 0x03);      // 5-byte addresses.
    radio.writeReg(SETUP_RETR, 0x5F);    // 1500 us, 15 retries.
    radio.writeReg(RF_CH, 0x60);
    radio.writeReg(RF_SETUP, 0x00);      // 1 Mbps, PA_MIN.
    radio.writeReg(DYNPD, 0x00);
    radio.writeReg(FEATURE, 0x00);
    radio.writeRegBytes(RX_ADDR_P0, ADDRESS, sizeof(ADDRESS));
    radio.writeReg(RX_PW_P0, PAYLOAD_SIZE);
    radio.command(FLUSH_RX);
    radio.command(FLUSH_TX);
    radio.writeReg(STATUS, MASK_RX_DR | MASK_TX_DS | MASK_MAX_RT);
    radio.writeReg(CONFIG, 0x0F);  // Power up, PRX, CRC on, 2-byte CRC.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ce.set(true);

    std::signal(SIGINT, stopReceiver);
    std::signal(SIGTERM, stopReceiver);

    std::cout << "Raspberry Pi direct NRF24 receiver started\n"
              << "CE GPIO: " << cePin << ", SPI: " << spiPath
              << ", address: C2 C2 C2 C2 C2, channel: 0x60, data rate: 1Mbps\n"
              << "STATUS=" << hexByte(radio.command(NOP))
              << " CONFIG=" << hexByte(radio.readReg(CONFIG))
              << " RF_CH=" << hexByte(radio.readReg(RF_CH))
              << " RF_SETUP=" << hexByte(radio.readReg(RF_SETUP))
              << " FIFO=" << hexByte(radio.readReg(FIFO_STATUS)) << "\n";

    while (running) {
      if (radio.readReg(FIFO_STATUS) & RX_EMPTY) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }

      uint8_t payload[PAYLOAD_SIZE] = {};
      radio.readPayload(payload, PAYLOAD_SIZE);
      radio.writeReg(STATUS, MASK_RX_DR);

      TemperatureData data{};
      std::memcpy(&data, payload, sizeof(data));

      const float temperatureC = data.tempC_x100 / 100.0f;
      const float temperatureF = data.tempF_x100 / 100.0f;

      std::cout << "Raw:";
      for (uint8_t byte : payload) {
        std::cout << ' ' << hexByte(byte);
      }
      std::cout << "   Temperature in C : " << temperatureC << " C   "
                << "Temperature in F : " << temperatureF << " F\n";
    }

    ce.set(false);
    radio.writeReg(CONFIG, 0x0C);
    std::cout << "Receiver stopped\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << "\n";
    return 1;
  }

  return 0;
}

/*
 * support.h
 *
 *  Created on: Sep 21, 2020
 *      Author: root
 */

#ifndef SUPPORT_H_
#define SUPPORT_H_

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include<unistd.h>

#include<stdio.h>
#include<fcntl.h>
#include<sys/ioctl.h>
#include<linux/spi/spidev.h>
#include<unistd.h>
#define SPI_PATH "/dev/spidev1.0"

#define HEX_CHARS      "0123456789ABCDEF"
#define nRF24_WAIT_TIMEOUT         (uint32_t)0x000FFFFF


#define CE_pin "/sys/class/gpio/gpio60/value"			//BBB Linux debian CE GPIO60 P9_12
#define CE_direction "/sys/class/gpio/gpio60/direction"

#define CSN_pin "/sys/class/gpio/gpio30/value"			//BBB Linux debian CSN GPIO30 P9_11
#define CSN_direction "/sys/class/gpio/gpio30/direction"

//SPI DEV
int fd;
//

void runRadio(uint8_t *ROLE);
void RX_single(void);
void TX_single(void);
void UART_SendStr(char *string);
void UART_SendChar(char b);
void read_ADC(void);

void Toggle_LED() ;

///BBB functions
int spi_initas();
int transfer(int fd, unsigned char send[], unsigned char rec[], int len);
uint8_t nRF24_LL_RW(uint8_t data);

void init_CE(char value[]);
void set_CE(char value[]);

void init_CSN(char value[]);
void set_CSN(char value[]);

void nRF24_CE_L();
void nRF24_CE_H();
void nRF24_CSN_L();
void nRF24_CSN_H();

typedef enum {
	nRF24_TX_ERROR  = (uint8_t)0x00, // Unknown error
	nRF24_TX_SUCCESS,                // Packet has been transmitted successfully
	nRF24_TX_TIMEOUT,                // It was timeout during packet transmit
	nRF24_TX_MAXRT                   // Transmit failed with maximum auto retransmit count
} nRF24_TXResult;

nRF24_TXResult tx_res;

nRF24_TXResult nRF24_TransmitPacket(uint8_t *pBuf, uint8_t length);




static inline void Delay_ms(uint32_t ms) { usleep(ms*1000); }


#endif /* SUPPORT_H_ */

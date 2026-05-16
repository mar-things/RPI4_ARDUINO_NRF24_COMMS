/*
 * support.c
 *
 *  Created on: Sep 21, 2020
 *      Author: root
 */


#include "support.h"
#include "nrf24.h"


// send buffer and receive buffer
uint32_t data_to_send[10];
uint32_t data_received[10];

uint8_t RX_FLAG=0;


uint8_t nRF24_payload[32];

// Pipe number
nRF24_RXResult pipe_nrf;

uint32_t i,j,k;

// Length of received payload
uint8_t payload_length=10;



void UART_SendStr(char *string) {
	printf("%s", string);
}

void UART_SendChar(char b) {
	printf("%c", b);
}


void Toggle_LED() {
	//TODO toogle user led
}

 uint8_t nRF24_LL_RW(uint8_t data) {
    // Wait until TX buffer is empty
    uint8_t result=0;
   // nRF24_CSN_L();
    if(transfer(fd, (unsigned char*) &data,(unsigned char*) &result,1)==-1){
    		perror("transfer failed");
    		return -1;
    	}
   // nRF24_CSN_H();
    return result;
}


//  At debian instead of UART PRINT to console


void UART_SendBufHex(char *buf, uint16_t bufsize) {
	uint16_t i;
	char ch;
	for (i = 0; i < bufsize; i++) {
		ch = *buf++;
		UART_SendChar(HEX_CHARS[(ch >> 4)   % 0x10]);
		UART_SendChar(HEX_CHARS[(ch & 0x0f) % 0x10]);
	}
}
void UART_SendHex8(uint16_t num) {
	UART_SendChar(HEX_CHARS[(num >> 4)   % 0x10]);
	UART_SendChar(HEX_CHARS[(num & 0x0f) % 0x10]);
}

void UART_SendInt(int32_t num) {
	char str[10]; // 10 chars max for INT32_MAX
	int i = 0;
	if (num < 0) {
		UART_SendChar('-');
		num *= -1;
	}
	do str[i++] = (char) (num % 10 + '0'); while ((num /= 10) > 0);
	for (i--; i >= 0; i--) UART_SendChar(str[i]);
}




// start NRF24L01

void runRadio(uint8_t *ROLE)
{
	//INIT SPI
	spi_initas();
	//INIT GPIO and SET CE and CSN to HIGH
	init_CE("out");
	init_CSN("out");
	set_CSN("1");
	set_CE("1");

	// wait for 500 ms
	usleep(500000);

	UART_SendStr("\r\nBBB is ready\r\n");


		// Check presence of the nRF24L01+

		if (!nRF24_Check()) {
			UART_SendStr("FAIL\r\n");
			while(1);
		}

		else {UART_SendStr("OK\r\n");}


		// Initialize the nRF24L01 to its default state
		nRF24_Init();


		if (ROLE==1) // TX
		{
			UART_SendStr("NRF24L01 -> TX\r\n");
			TX_single();

		}

		else
		{
			UART_SendStr("NRF24L01 -> RX\r\n");
			RX_single();
		}





}


int spi_initas(){


	//SPI MODE 0
		uint8_t mode=0;

// OPEN SPI and set MODE for RD and WR
		if((fd=open(SPI_PATH, O_RDWR))<0){
				perror("SPI is missing\n");
				return -1;
			}
		if((ioctl(fd, SPI_IOC_WR_MODE, &mode)==-1)){
					perror("SPI can't set mode\n");
					return -1;
				}
		if((ioctl(fd, SPI_IOC_RD_MODE, &mode)==-1)){
						perror("SPI can't set mode\n");
						return -1;
					}
		printf("%d mode \n", mode);

		return 0;
}


// transfer over SPI
// fd pointer to spi
// send array for sending
// rec array for receiving
// len size of arrays in bytes
int transfer(int fd, unsigned char send[], unsigned char rec[], int len){
// configuring SPI by help of transfer structure
	struct spi_ioc_transfer transfer;
	transfer.tx_buf = (unsigned long ) send;
	transfer.rx_buf = (unsigned long ) rec;
	transfer.len = len;
	transfer.speed_hz = 1000000;
	transfer.bits_per_word = 8;
	transfer.delay_usecs = 0;
	transfer.cs_change = 0;
	transfer.tx_nbits=0;
	transfer.rx_nbits=0;
	transfer.pad=0;
	//send and receive data over SPI
	int status = ioctl(fd, SPI_IOC_MESSAGE(1), &transfer);

//	fflush(stdout);

	if(status<0){
		printf(" %d \n", status);
		perror("SPI -> FAIL");
		return -1;
	}

	return status;

}

// INIT GPIO for CE
void init_CE(char value[]){
		FILE* fp;
		fp=fopen(CE_direction, "w+");
		fprintf(fp,"%s", value);
		fclose(fp);
}

// SET CE using "1" or "0"
void set_CE(char value[]){
	FILE* fp;
	//char fullFileName[100];
	//sprintf(fullFileName, gpio_pin "%s", filename);
	fp=fopen(CE_pin, "w+");
	fprintf(fp,"%s", value);
	fclose(fp);
}
// INIT GPIO for CSN
void init_CSN(char value[]){
	FILE* fp;
		//char fullFileName[100];
		//sprintf(fullFileName, gpio_pin "%s", filename);
		fp=fopen(CSN_direction, "w+");
		fprintf(fp,"%s", value);
		fclose(fp);
}

// SET CSN using "1" or "0"
void set_CSN(char value[]){
	FILE* fp;
	//char fullFileName[100];
	//sprintf(fullFileName, gpio_pin "%s", filename);
	fp=fopen(CSN_pin, "w+");
	fprintf(fp,"%s", value);
	fclose(fp);
}

void nRF24_CE_L() {
	set_CE("0");
}

void nRF24_CE_H() {
	set_CE("1");
}

void nRF24_CSN_L() {
	set_CSN("0");
}

void nRF24_CSN_H() {
	set_CSN("1");
}

void RX_single(void)

{
	// This is simple receiver with one RX pipe:
		//   - pipe#1 address: '0xE7 0x1C 0xE3'
		//   - payload: 5 bytes
		//   - RF channel: 115 (2515MHz)
		//   - data rate: 250kbps (minimum possible, to increase reception reliability)
		//   - CRC scheme: 2 byte

	    // The transmitter sends a 5-byte packets to the address '0xE7 0x1C 0xE3' without Auto-ACK (ShockBurst disabled)

	    // Disable ShockBurst for all RX pipes
	    nRF24_DisableAA(0xFF);

	    // Set RF channel
	    nRF24_SetRFChannel(115);

	    // Set data rate
	    nRF24_SetDataRate(nRF24_DR_250kbps);

	    // Set CRC scheme
	    nRF24_SetCRCScheme(nRF24_CRC_2byte);

	    // Set address width, its common for all pipes (RX and TX)
	    nRF24_SetAddrWidth(3);

	    // Configure RX PIPE#1
	    static const uint8_t nRF24_ADDR[] = { 0xE7, 0x1C, 0xE3 };
	    nRF24_SetAddr(nRF24_PIPE1, nRF24_ADDR); // program address for RX pipe #1
	    nRF24_SetRXPipe(nRF24_PIPE1, nRF24_AA_OFF, payload_length); // Auto-ACK: disabled, payload length: 5 bytes

	    // Set operational mode (PRX == receiver)
	    nRF24_SetOperationalMode(nRF24_MODE_RX);

	    // Wake the transceiver
	    nRF24_SetPowerMode(nRF24_PWR_UP);

	    // Put the transceiver to the RX mode
	    nRF24_CE_H();


	    UART_SendStr("STARTAS");
	    nRF24_ClearIRQFlags();

	    // The main loop
	    while (1)
	    {
	    	//
	    	// Constantly poll the status of the RX FIFO and get a payload if FIFO is not empty
	    	//
	    	// This is far from best solution, but it's ok for testing purposes
	    	// More smart way is to use the IRQ pin :)
	    	//



	    	if (nRF24_GetStatus_RXFIFO() != nRF24_STATUS_RXFIFO_EMPTY)
	    	{
	    	// Get a payload from the transceiver

	    		pipe_nrf=nRF24_RX_EMPTY;
	    		
	    		while (pipe_nrf==nRF24_RX_EMPTY)
	    		{
	    			pipe_nrf = nRF24_ReadPayload(nRF24_payload, &payload_length);
	    		}
	    		UART_SendStr("\r\n");
	    		UART_SendStr("RCV PIPE#");
	    		UART_SendInt(pipe_nrf);
	    		UART_SendStr("\r\n");
	    	

	    		// Print a payload contents to UART

	    		UART_SendStr(" PAYLOAD:>");
	    		UART_SendStr(nRF24_payload);
	    		UART_SendStr("<\r\n");



	    		nRF24_ClearIRQFlags();

	    	}
	    }

}


void TX_single(void)
{

	// This is simple transmitter (to one logic address):
		//   - TX address: '0xE7 0x1C 0xE3'
		//   - payload: 5 bytes
		//   - RF channel: 115 (2515MHz)
		//   - data rate: 250kbps (minimum possible, to increase reception reliability)
		//   - CRC scheme: 2 byte

	    // The transmitter sends a 5-byte packets to the address '0xE7 0x1C 0xE3' without Auto-ACK (ShockBurst disabled)

	    // Disable ShockBurst for all RX pipes
	    nRF24_DisableAA(0xFF);

	    // Set RF channel
	    nRF24_SetRFChannel(115);

	    // Set data rate
	    nRF24_SetDataRate(nRF24_DR_250kbps);

	    // Set CRC scheme
	    nRF24_SetCRCScheme(nRF24_CRC_2byte);

	    // Set address width, its common for all pipes (RX and TX)
	    nRF24_SetAddrWidth(3);

	    // Configure TX PIPE
	    static const uint8_t nRF24_ADDR[] = { 0xE7, 0x1C, 0xE3 };
	    nRF24_SetAddr(nRF24_PIPETX, nRF24_ADDR); // program TX address

	    // Set TX power (maximum)
	    nRF24_SetTXPower(nRF24_TXPWR_0dBm);

	    // Set operational mode (PTX == transmitter)
	    nRF24_SetOperationalMode(nRF24_MODE_TX);

	    // Clear any pending IRQ flags
	    nRF24_ClearIRQFlags();

	    // Wake the transceiver
	    nRF24_SetPowerMode(nRF24_PWR_UP);


	    // The main loop
	    j = 0;
	    payload_length = 8;

	    while (1) {
	    	// Prepare data packet
	    	for (i = 0; i < payload_length; i++) {
	    		nRF24_payload[i] = j++;
	    		if (j > 0x000000FF) j = 0;
	    	}

	    	// Print a payload
	    	UART_SendStr("PAYLOAD:>");
	    	UART_SendBufHex((char *)nRF24_payload, payload_length);
	    	UART_SendStr("< ... TX: ");

	    	// Transmit a packet
	    	tx_res = nRF24_TransmitPacket(nRF24_payload, payload_length);
	    	switch (tx_res) {
				case nRF24_TX_SUCCESS:
					UART_SendStr("OK");
					Toggle_LED();
					break;
				case nRF24_TX_TIMEOUT:
					UART_SendStr("TIMEOUT");
					break;
				case nRF24_TX_MAXRT:
					UART_SendStr("MAX RETRANSMIT");
					break;
				default:
					UART_SendStr("ERROR");
					break;
			}
	    	UART_SendStr("\r\n");

	    	// Wait ~0.5s
	    	Delay_ms(500);
	    }


}

nRF24_TXResult nRF24_TransmitPacket(uint8_t *pBuf, uint8_t length) {
	volatile uint32_t wait = nRF24_WAIT_TIMEOUT;
	uint8_t status;

	// Deassert the CE pin (in case if it still high)
	nRF24_CE_L();

	// Transfer a data from the specified buffer to the TX FIFO
	nRF24_WritePayload(pBuf, length);

	// Start a transmission by asserting CE pin (must be held at least 10us)
	nRF24_CE_H();

	// Poll the transceiver status register until one of the following flags will be set:
	//   TX_DS  - means the packet has been transmitted
	//   MAX_RT - means the maximum number of TX retransmits happened
	// note: this solution is far from perfect, better to use IRQ instead of polling the status
	do {
		status = nRF24_GetStatus();
		if (status & (nRF24_FLAG_TX_DS | nRF24_FLAG_MAX_RT)) {
			break;
		}
	} while (wait--);

	// Deassert the CE pin (Standby-II --> Standby-I)
	nRF24_CE_L();

	if (!wait) {
		// Timeout
		return nRF24_TX_TIMEOUT;
	}

	// Check the flags in STATUS register
	UART_SendStr("[");
	UART_SendHex8(status);
	UART_SendStr("] ");

	// Clear pending IRQ flags
    nRF24_ClearIRQFlags();

	if (status & nRF24_FLAG_MAX_RT) {
		// Auto retransmit counter exceeds the programmed maximum limit (FIFO is not removed)
		return nRF24_TX_MAXRT;
	}

	if (status & nRF24_FLAG_TX_DS) {
		// Successful transmission
		return nRF24_TX_SUCCESS;
	}

	// Some banana happens, a payload remains in the TX FIFO, flush it
	nRF24_FlushTX();

	return nRF24_TX_ERROR;
}



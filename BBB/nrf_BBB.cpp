//============================================================================
// Name        : nrf_BBB.cpp
// Author      : a
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================


#include <iostream>
using namespace std;


extern "C" void runRadio(uint8_t *ROLE);

#define TX 1
#define RX 0
#include "support.h"
#include "nrf24.h"

int main() {
	cout << "!!!Hello NRF!!!" << endl; // prints !!!Hello World!!!

	 //spi_initas();
	runRadio(RX);

	return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include "libsoc_spi.h"
#include "libsoc_debug.h"

#define SPI_DEVICE 0
#define CHIP_SELECT 0

int main () {

   uint8_t tx[2];
   uint8_t rx[2];
   uint16_t prodID = 0, temp = 0;
   uint32_t i = 0, j = 0;
   uint32_t speed = 1000000;

   printf("Before initializing the SPI\n");

   spi* spi_dev = libsoc_spi_init(SPI_DEVICE, CHIP_SELECT);

   printf("Initialized SPI\n");

   libsoc_spi_set_mode(spi_dev, MODE_0);

   printf("Just set the mode\n");


   libsoc_spi_set_speed(spi_dev, speed);

   printf("Just set the speed\n");

   libsoc_spi_set_bits_per_word(spi_dev, BITS_16);

   printf("Did all of setup\n");

   for (; i < 200000; i++) {
      for (; j < 30000; j++) {

      }
   }


   tx[0] = 0x7E;
   tx[1] = 0x00;
   //tx[2] = 0x0E;
   //tx[3] = 0x00;

   libsoc_spi_rw(spi_dev, tx, rx, 2);

   prodID = prodID >> 8 | rx[0];
   prodID = prodID | rx[1];

   //temp = temp >> 8 | rx[2];
   //temp = temp | rx[3];

   printf("Product ID is %d\n", prodID);
   printf("Internal Temp is %d\n", temp);

   libsoc_spi_free(spi_dev);

   return 0;
}

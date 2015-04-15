/**
 *  If we need help with the I/O functions, the website below may be helpful:
 *  http://www.element14.com/community/community/knode/single-board_computers/next-gen_beaglebone/blog/2013/10/10/bbb--beaglebone-black-io-library-for-c
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iolib.h>
#include "cs203_dc.h"


#define ENABLE_DELAY 100


// Remove the enable (or make it really small) when you add SD Card code or IMU code
int check_trigger() {
   static int enable = 0;
   int trig = 0;
   
   if (enable == ENABLE_DELAY) {
      if (is_high(8, 15)) {
         printf("TRIGGERED!!!\n");
         enable = 0;
         trig = 1;
      }
   }
   else {
      enable++;
   }
   
   return trig;
}


int main () {
   // Variables and other stuffs here
   
   
   iolib_init(); // Fill in with args
   
   iolib_setdir(8, 11, DIR_OUT);  // LED for RFID Init
   iolib_setdir(8, 12, DIR_IN);   // Start Button, disabled
   iolib_setdir(8, 15, DIR_IN);   // Trigger Input
   
   pin_low(8, 11);
   
   init_rfid();
   
   // Once RFID is initialized, turn LED on Header 8, Pin 11 HIGH
   pin_high(8, 11);
   printf("RFID Initialized, ready to run program\n");
   
   // If we need to initialize the SD Card and/or IMU, that code will go
   // right here and we will have an LED for each one.
   
   // while (is_high(8, 12) == 0) {} // This will loop until the Button on Header 8, Pin 12 is pressed
   
   while (1) {
      // Get data from IMU
      
      if (check_trigger()) {  // If trigger is triggered, call run_rfid
         run_rfid();          // This runs the RFID to read the tag
      }                       // add args to this function
      
      // Write to SD Card
   }
   
   return 0;
}
/**
 *  If we need help with the I/O functions, the website below may be helpful:
 *  http://www.element14.com/community/community/knode/single-board_computers/next-gen_beaglebone/blog/2013/10/10/bbb--beaglebone-black-io-library-for-c
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include <iolib.h>
#include "cs203_dc.h"

#define RFID_RUNS 8

// Remove the enable (or make it really small) when you add SD Card code or IMU code
int check_trigger() {
   int trig = 0;

      if (is_high(8, 11)) {
         //printf("TRIGGERED!!!\n");
         trig = 1;
      }

   return trig;
}


int main () {
   // Variables and other stuffs here
   printf("\nSystem program starts\n");

   char *my_tag_info = calloc(1600, sizeof(char));
   CS203DC_Handle *rfid_fd;
   int rfid_argc = 3;
   char *rfid_argv[] = {"a.out", "192.168.25.203", "-tag"};
   char *tag_id, trigg[1], newline[1];
   FILE *fp;
   
   char prev_tag_id[50];
   time_t init_time;
   char *printTime;

   printf("Initializing I/O...");
   iolib_init(); // Fill in with args
   printf("I/O is initialized\nInitializing RFID...\n");

   iolib_setdir(8, 17, DIR_IN);    // End Button for Loop
   iolib_setdir(8, 11, DIR_IN);    // Trigger Input

   pin_low(8, 11);
   
   rfid_fd = init_rfid(rfid_argc, rfid_argv);
   printf("RFID is initialized\n\n");

   // If we need to initialize the SD Card and/or IMU, that code will go right here
   fp = fopen("testdata.csv", "w");

   pin_high(8, 16);

   trigg[0] = '0';
   newline[0] = '\n';

   printf("System has started, press the button to end it or wait 200 seconds\n");

   init_time = time(NULL);
   printf("%d\n", init_time);
   while (is_high(8, 17) == 0 &&
         (time(NULL) - init_time) < 200000) { // This will loop until the Button on Header 8, Pin 9 is pressed
      // Get data from IMU
      
      if (trigg[0] == '1') {
         if (check_trigger() == 0) {
            printf("Trigger going low\n");
            
            printTime = ctime(time(NULL));
            fwrite(printTime, sizeof(char), strlen(printTime), fp);
            fwrite("     ", sizeof(char), 5, fp);
            fwrite(trigg, sizeof(char), 1, fp);
            fwrite(newline, sizeof(char), 1, fp);
      
            trigg[0] = '0';
         }
      } else if (trigg[0] == '0') {
         if (check_trigger()) {
            printf("Trigger going high\n");
            
            printTime = ctime(time(NULL));
            fwrite(printTime, sizeof(char), strlen(printTime), fp);
            fwrite("     ", sizeof(char), 5, fp);
            fwrite(trigg, sizeof(char), 1, fp);
            fwrite(newline, sizeof(char), 1, fp);
      
            trigg[0] = '1';
         }
      }
      
      run_rfid(rfid_fd, &my_tag_info);
      tag_id = strtok(my_tag_info, ">");
      
      if (!strcmp(tag_id, prev_tag_id)) {
         if (tag_id[0] != '<') {
            printf("Passed tag\n");
            
            printTime = ctime(time(NULL));
            fwrite(printTime, sizeof(char), strlen(printTime), fp);
            fwrite("     ", sizeof(char), 5, fp);
            fwrite("Left range of tag", sizeof(char), 17, fp);
            fwrite(newline, sizeof(char), 1, fp);
         } else {
            printf("Found RFID tag\n");
            
            printTime = ctime(time(NULL));
            fwrite(printTime, sizeof(char), strlen(printTime), fp);
            fwrite("     ", sizeof(char), 5, fp);
            fwrite(my_tag_info, sizeof(char), 39, fp);
            fwrite(newline, sizeof(char), 1, fp);
         }
      }

      memset(my_tag_info, 0, sizeof(char) * 1600);
      
      //prev_tag_id = tag_id;
      memcpy(prev_tag_id, tag_id, sizeof(char) * 50);
   }
   
   // close the SD Card file
   fclose(fp);

   iolib_free();
   free(my_tag_info);


   return 0;
}

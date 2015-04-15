#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

float getX(float x, float angle) {

   return cos(angle) * x;

}

float getY(float y, float angle) {

   return sin(angle) * y;

}

float findPosition(float velI, float velF, float time) {
   return .5 * (velI + velF) * time;
}

float positionFromAcceleration(float initialVelocity, float time, float acceleration) {
   return (initialVelocity * time) + (.5 * acceleration * pow(time, 2));
}

int main(int argc, char **argv) {

   FILE *inputFile = fopen("Datalog.csv", "r");

   char buf[100];
   char *curr;

   // Represents the acceleration at each deltaT from the IMU
   float accelX = 0, accelY = 0, accelZ = 0;
   // Represents the delta velocity from the IMU
   float deltX = 0, deltY = 0, deltZ = 0;
   // Represents the starting velocity for each deltaT
   float currVelX = 0, currVelY = 0, currVelZ = 0;
   // Represents the starting position for each deltaT
   float currPosX = 0, currPosY = 0, currPosZ = 0;

   float deltaTime = .00040650406504;
   deltaTime = 1.0/2460.0;

   while( fgets(buf, 100, inputFile) != NULL ) {

      curr = strtok(buf, ",");  // Discard the sample number
      curr = strtok(NULL, ",");
      accelX = atof(curr) * 9.81;
      curr = strtok(NULL, ",");
      accelY = atof(curr) * 9.81;
      curr = strtok(NULL, ",");
      accelZ = atof(curr);

	//printf("accelX: %f", accelX);

      deltX = accelX * deltaTime;
      deltY = accelY * deltaTime;
      deltZ = accelZ * deltaTime;


      //currPosX += findPosition(currVelX, currVelX + deltX, deltaTime);
      //currPosY += findPosition(currVelY, currVelY + deltY, deltaTime);
      //currPosZ += findPosition(currVelZ, currVelZ + deltZ, deltaTime);
      currPosX += positionFromAcceleration(currVelX, deltaTime, accelX);
      currPosY += positionFromAcceleration(currVelY, deltaTime, accelY);
      currPosZ += positionFromAcceleration(currVelZ, deltaTime, accelZ);


      currVelX += deltX;
      currVelY += deltY;
      currVelZ += deltZ;
   }

   printf("currPosX: %f\n", currPosX);
   printf("currPosY: %f\n", currPosY);
   printf("currPosZ: %f\n", currPosZ);

   
   //printf("xy: %f\n", getX(currPosX, 45)+ getY(currPosY, 45));
   printf("xy: %f\n", sqrt( pow(currPosX, 2) + pow(currPosY, 2) ));

   return 0;
}

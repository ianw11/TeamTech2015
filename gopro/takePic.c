#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <curl/curl.h>

#define NUM_HEADER_LINES 7
#define REQUEST_BUFF_SIZE 1000

/*
   WHEN COMPILING, MAKE SURE YOU ADD '-l curl' TO THE GCC COMMAND OR ELSE CURL WON'T BE INCLUDED
   $ gcc -l curl *.c
   
   Code borrowed in large part from:
   http://cboard.cprogramming.com/c-programming/142841-sending-http-get-request-c.html
   
   
   http://10.5.5.9/gp/gpMediaList
   http://10.5.5.9:8080/videos/DCIM/100GOPRO/
   http://10.5.5.9/gp/gpControl/command/mode?p=0  for video mode
   
   For streaming:
   http://10.5.5.9:8080/gp/gpExec?p1=gpStreamA9&c1=stop
   run the keepalive.py script to keep the stream alive
   $ ffplay udp://10.5.5.9:8554
   
*/

size_t curlCallback(char *ptr, size_t size, size_t nmemb, void *userdata) {
   /* 'ptr' is the delivered data
      size of data is 'size' * 'nmemb'
      
      data is not zero terminated
   */
   //size_t ret = 0;
   
   printf("In Callback\n");
   
   FILE* outFile = fopen("curlOut.mp4", "w");
   if (!outFile) {
      printf("File couldn't be opened\n");
      return 1;
   }
   fwrite(ptr, size, nmemb, outFile);
   
   fclose(outFile);
   
   return CURLE_OK;
}

void doCurl() {
   CURL *curl;
   CURLcode res;
   
   printf("Performing curl\n");
   
   FILE* outFile = fopen("curlOut.mp4", "w");
   if (!outFile) {
      printf("File couldn't be opened\n");
      return;
   }
   
   
   
   curl = curl_easy_init();
   if (!curl) {
      printf("Error making curl\n");
      exit(1);
   }
   curl_easy_setopt(curl, CURLOPT_URL, "10.5.5.9:8080/videos/DCIM/100GOPRO/GOPR0031.MP4");
   //curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlCallback);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, outFile);
   res = curl_easy_perform(curl);
   if (res != CURLE_OK) {
      printf("Error %d with curl: %s\n", res, curl_easy_strerror(res));
   }
   curl_easy_cleanup(curl);
   
   fclose(outFile);
   
   printf("\n");
}


int main(int argc, char **argv) {
   char arg[500];
   char firstHalf[500];
   char secondHalf[500];
   char request[REQUEST_BUFF_SIZE];
   struct hostent *server;
   struct sockaddr_in serveraddr;
   int port = 8080;

   strcpy(firstHalf, "10.5.5.9");
   strcpy(secondHalf, "/gp/gpControl/command/shutter?p=1");
   //strcpy(secondHalf, "/videos/DCIM/100GOPRO/GOPR0033.MP4");

   int tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
   if (tcpSocket < 0) {
      printf("Error opening socket!\n");
      exit (1);
   }

   server = gethostbyname(firstHalf);
   if (server != NULL) {
      printf("\n%s = ", server->h_name);
      unsigned int j = 0;
      while (server->h_addr_list[j] != NULL) {
         printf("%s", (char *) inet_ntoa( * ((struct in_addr*)(server->h_addr_list[j])) ));
         j++;
      }
      printf("\n");

      bzero( (char *)&serveraddr, sizeof(serveraddr));
      serveraddr.sin_family = AF_INET;

      bcopy( (char *)server->h_addr, (char *)&serveraddr.sin_addr.s_addr, server->h_length);

      serveraddr.sin_port = htons(port);

      if (connect(tcpSocket, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) >= 0) {
         
         bzero(request, REQUEST_BUFF_SIZE);
         sprintf(request, "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: keep-alive\r\nAccept: text/html\r\nUser-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_5)\r\nAccept-Encoding: gzip\r\nAccept-Language: en-US\r\n\r\n", secondHalf, firstHalf, port);

         printf("\n%s", request);

         if (send(tcpSocket, request, strlen(request), 0) >= 0) {

            sleep(1);
            bzero(request, REQUEST_BUFF_SIZE);
            recv(tcpSocket, request, REQUEST_BUFF_SIZE - 1, 0);


            printf("RESPONSE:");
            printf("\n%s", request);
            printf("\n");


         } else {
            printf("send failed!\n");
         }
      } else {
         printf("connect failed!\n");
      }
   } else {
      printf("gethostbyname failed!\n");
   }

   close(tcpSocket);
   
   //doCurl();

   return 0;
}

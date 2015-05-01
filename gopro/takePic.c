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
   http://10.5.5.9:8080/gp/gpExec?p1=gpStreamA9&c1=stop OR restart OR start (I'm pretty sure)
   run the keepalive.py script to keep the stream alive
   $ ffplay udp://10.5.5.9:8554
   
*/

/* Ignore this function */
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

/* Ignore this function */
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

   // Set the target IP
   strcpy(firstHalf, "10.5.5.9");
   // Set the target command
   strcpy(secondHalf, "/gp/gpControl/command/shutter?p=1");
   //strcpy(secondHalf, "/videos/DCIM/100GOPRO/GOPR0033.MP4");

   // Create a socket using (supposedly) standard flags
   int tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
   if (tcpSocket < 0) {
      printf("Error opening socket!\n");
      exit (1);
   }

   // server (which is a hostent [I don't know either]) gets set up here.
   // If it's valid, the code continues
   server = gethostbyname(firstHalf);
   if (server != NULL) {
      
      // This prints out the h_name value of the server (not sure what h_name is)
      printf("\n%s = ", server->h_name);
      
      // And I have no idea why we use this loop either
      unsigned int j = 0;
      while (server->h_addr_list[j] != NULL) {
         printf("%s", (char *) inet_ntoa( * ((struct in_addr*)(server->h_addr_list[j])) ));
         j++;
      }
      printf("\n");

      // Here, we zero out the serveraddr struct
      bzero( (char *)&serveraddr, sizeof(serveraddr));
      // And set the sin_family (I don't know) to AF_INET
      serveraddr.sin_family = AF_INET;

      // I don't know why we're copying stuff here either
      bcopy( (char *)server->h_addr, (char *)&serveraddr.sin_addr.s_addr, server->h_length);

      // Not sure what sin_port is either (but I assume it involves 8080)
      serveraddr.sin_port = htons(port);


      // FINALLY we connect to the socket
      if (connect(tcpSocket, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) >= 0) {
         
         // Then we clear the request buffer
         bzero(request, REQUEST_BUFF_SIZE);
         // And write the entire HTTP request into it
         sprintf(request, "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: keep-alive\r\nAccept: text/html\r\nUser-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_5)\r\nAccept-Encoding: gzip\r\nAccept-Language: en-US\r\n\r\n", secondHalf, firstHalf, port);

         // This just prints out the contents of request
         printf("\n%s", request);

         // Now that we've established a connection to the server and
         // have a request for the server, we finally send the
         // request to the server (in this case, the server is
         // the GoPro
         if (send(tcpSocket, request, strlen(request), 0) >= 0) {

            // HACK CODE! Sleeping is typically a hack...
            sleep(1);
            
            // After the request has been sent, clear the request buffer
            // (Yes, we SHOULD use another buffer, but whatever)
            bzero(request, REQUEST_BUFF_SIZE);
            // And write the data we've received into this buffer
            recv(tcpSocket, request, REQUEST_BUFF_SIZE - 1, 0);


            // Print out what we got
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

   // When we're all done, close the socket (terminating the connection to the server/GoPro)
   close(tcpSocket);
   
   // And we're not going to bother with curl, since we want a stream from the GoPro
   // and not files saved (there won't be an SD card in the actual GoPro)
   //doCurl();

   return 0;
}

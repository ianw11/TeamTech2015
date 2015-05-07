# This code is built for Python 2.7.5 (on OS X).
# To update the code for Python 3, replace httplib with http.client
# ie: import http.client

# In addition, raw_input can be changed for Python 3

# ffplay -vcodec libx264 udp://10.5.5.9:8554

import httplib
import os
import signal
from socket import *

goProURL = "10.5.5.9"
GOPRO_UDP_PORT = 8554

goProMode = -1

childPIDs = []

########### FUNCTIONS TO HELP WITH MULTIPROCESSING #############

def execFile(file):
   currDir = os.getcwd()
   fullDir = currDir + "/" + file
   res = os.system('python ' + fullDir)
   exit(0)

def killExecdFile(ndx):
   targetpid = childPIDs[ndx]
   # The +1 kills the actual child process, not the python process that started it
   os.kill(targetpid + 1, signal.SIGTERM)
   del childPIDs[int(input) - numChoices]

def launchPythonScript(file):
   newPid = os.fork()
   
   if newPid == 0:
      execFile(file)
   else:
      print "Child process (pid: " + str(newPid) + ") launched"
      childPIDs.append(newPid)


######### FUNCTIONS TO INTERACT WITH THE GOPRO #############


def captureGoProUDPPackets():
   serversock = socket(AF_INET, SOCK_DGRAM) # UDP
   
   serversock.bind(("localhost", 10000))
   #serversock.sendto('0:0', ('10.5.5.9', 8554))
   
   while True:
      data, addr = serversock.recvfrom(1024)
      if len(data) == 0:
         break
      else:
         # Do something with the received data!
         print data


def performGoProGet(command, usePort):
   addr = goProURL
   if usePort:
      addr += ":8080"
   conn = httplib.HTTPConnection(goProURL)
   conn.request("GET", command);
   r1 = conn.getresponse()
   
   # This will read the entire content
   data1 = r1.read()

   conn.close()
   
   return data1

def performGoProCommand(command, usePort):
   return performGoProGet("/gp/gpControl/command/" + command, usePort)

def takePicture():
   global goProMode
   # Sets the GoPro into Single Photo mode
   if goProMode != 1:
      performGoProCommand("mode?p=1", False)
      goProMode = 1
      
   # Then takes a picture
   return performGoProCommand("shutter?p=1", False)
   
   
def startAndRecordStream():
   global goProMode
   
   # Set the GoPro into Video Mode
   if goProMode != 0:
      performGoProCommand("mode?p=0", False)
      goProMode = 0
   
   # Run the GoPro keepalive script
   launchPythonScript('keepalive.py')
   
   # Start the stream
   performGoProGet('/gp/gpExec?p1=gpStream&c1=restart', True)
   
   # Capture the data
   captureGoProUDPPackets()
   
   # Stop the stream
   performGoProGet('/gp/gpExec?p1=gpStream&c1=stop', True)
   
   # Kill the keepalive
   killExecdFile( len(childPIDs) - 1 )



############# MAIN ###############


def main():
   choices = [ "Exit",
               "Take a picture",
               "Exec 'hello.py'",
               "Exec 'keepalive.py'",
               "Dump UDP Stream"
               ]
   numChoices = len(choices)
   
   while(True):
      print "Select one of the following options:"
      for i in range(0, numChoices):
         print str(i) + ': ' + choices[i]
      
      
      if len(childPIDs) > 0:
         size = len(childPIDs)
         for i in range(0, size):
            print str(numChoices + i) + ": Kill process " + str(childPIDs[i])
      
      input = raw_input('Enter option: ')
      
      if input is '0':
         if len(childPIDs) > 0:
            size = len(childPIDs)
            for i in range(0, size):
               print 'Waiting for pid: ' + str(childPIDs[i]);
               os.waitpid(childPIDs[i], 0)
               
         # Always end the program when the user enters a 0
         return
      elif input is '1':
         print takePicture()
      elif input is '2':
         launchPythonScript('hello.py')
      elif input is '3':
         launchPythonScript('keepalive.py')
      elif input is '4':
         startAndRecordStream()
      elif int(input) >= numChoices and int(input) < (numChoices + len(childPIDs)):
         killExecdFile(int(input) - numChoices)
      else:
         print "Illegal option"
      
      print
      print

###############################################################################
if __name__ == '__main__':
   main()

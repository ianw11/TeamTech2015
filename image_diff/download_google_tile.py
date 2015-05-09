import urllib2
import os, sys

CORE_URL = "https://maps.googleapis.com/maps/api/staticmap?"

user_agent = 'Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_8; de-at) AppleWebKit/533.21.1 (KHTML, like Gecko) Version/5.0.5 Safari/533.21.1'
headers = { 'User-Agent' : user_agent }

filename = 'img.png'

def downloadImage(lat, lon, zoom, type='satellite'):

   bytes = None

   url = CORE_URL
   url += 'center=(' + str(lat) + ',' + str(lon) + ')'
   url += '&'
   url += 'zoom=' + str(zoom)
   url += '&'
   url += 'size=1200x1200'
   url += '&'
   url += 'maptype=' + type
   print url
   
   print
   print
   
   try:
      req = urllib2.Request(url, data=None, headers=headers)
      response = urllib2.urlopen(req)
      bytes = response.read()
      
   except Exception, e:
      print e
   
   return bytes
   

def main():
   data = downloadImage(35.30202, -120.66146, 18)
   
   f = open(filename,'wb')
   f.write(data)
   f.close()


if __name__ == '__main__':
   main()

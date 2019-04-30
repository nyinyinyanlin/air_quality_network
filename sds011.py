import os
import serial
import time
import datetime
import argparse
import urllib2

parser = argparse.ArgumentParser(description="Python utility script for reading PM2.5 and PM10 data from SDS011 air quality sensor through serial input.")
parser.add_argument('-p','--port', help="Serial Port (Default: /dev/ttyUSB0)", default="/dev/ttyUSB0")
parser.add_argument('-o','--log', help="File path for data logging in csv format. (Default: aqlog.csv)", default="aqlog.csv")
parser.add_argument('-i','--interval', help="Data logging interval in seconds. (Default: 1)", default=1, type=int)
parser.add_argument('-k','--key', help="API Key for pushing the data to Thingspeak platform. If not given, data will not be pushed to Thingspeak.")
args = parser.parse_args()

ser = serial.Serial(args.port)
os.system("echo 'time, pm2.5, pm10' >"+args.log)

while True:
    ser.reset_input_buffer()
    data = ser.read(10)
    if data[0].encode('hex') == "\xaa".encode('hex') and data[1].encode('hex') == "\xc0".encode('hex'):
        print("Message header valid")
        print("Commander number valid")
    else:
        print("Invalid header bytes")
        continue
    pm25 = ((int(data[3].encode('hex'),16)*256)+int(data[2].encode('hex'),16))/10
    pm10 = ((int(data[5].encode('hex'),16)*256)+int(data[4].encode('hex'),16))/10
    print(":".join("{:02x}".format(ord(c)) for c in data))
    print("PM2.5: {}, PM10:{}".format(pm25,pm10))
    os.system(("echo '{}, {}, {}' >> "+args.log).format(str(datetime.datetime.now()),pm25,pm10))
    if(args.key):
    	try:
            urllib2.urlopen('https://api.thingspeak.com/update?api_key={}&field1={}&field2={}'.format(args.key,pm25,pm10))
        except Exception:
            print("An error occured!")
            pass
    print("")
    time.sleep(args.interval)

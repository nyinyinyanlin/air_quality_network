import os
import serial
import time
import datetime
import argparse
import urllib2

parser = argparse.ArgumentParser(description="Python utility script for reading CO2 PPM:0-5000 data from MH-Z19B CO2 NDIR sensor through serial input.")
parser.add_argument('-p','--port', help="Serial Port (Default: /dev/ttyUSB0)", default="/dev/ttyUSB0")
parser.add_argument('-o','--log', help="File path for data logging in csv format. (Default: co2log.csv)", default="co2log.csv")
parser.add_argument('-i','--interval', help="Data logging interval in seconds. (Default: 1)", default=1, type=int)
parser.add_argument('-k','--key', help="API Key for pushing the data to Thingspeak platform. If not given, data will not be pushed to Thingspeak.")
args = parser.parse_args()

ser = serial.Serial(args.port)
os.system("echo 'time, co2 ppm' >"+args.log)

# Inserted time delay to avoid race condition between serial port open and writing actions
time.sleep(2)

while True:
    #ser.write('ff0186000000000079'.decode('hex'))
    data = ser.read(9)
    if data[0].encode('hex') == "\xff".encode('hex') and data[1].encode('hex') == "\x86".encode('hex'):
        print("Message header valid")
        print("Commander number valid")
    else:
        print("Invalid header bytes")
        continue
    co2ppm = ((int(data[2].encode('hex'),16)*256)+int(data[3].encode('hex'),16))
    print(":".join("{:02x}".format(ord(c)) for c in data))
    print("CO2 PPM:{}".format(co2ppm))
    os.system(("echo '{}, {}' >> "+args.log).format(str(datetime.datetime.now()),co2ppm))
    if(args.key):
    	try:
            urllib2.urlopen('https://api.thingspeak.com/update?api_key={}&field3={}'.format(args.key,co2ppm))
        except Exception:
            print("An error occured!")
            pass
    print("")
    time.sleep(args.interval)

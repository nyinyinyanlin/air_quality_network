import os
import serial
import time
import datetime
import argparse
import urllib2

parser = argparse.ArgumentParser(description="Python utility script for reading CO2 PPM:0-5000 data from MH-Z19B CO2 NDIR sensor through serial input.")
parser.add_argument('-p','--pm_port', help="Serial Port for SDS011 PM2.5 & PM10 Sensor (Default: /dev/ttyUSB0)", default="/dev/ttyUSB0")
parser.add_argument('-c','--co2_port', help="Serial Port for CO2 NDIR Sensor (Default: /dev/ttyUSB1)", default="/dev/ttyUSB1")
parser.add_argument('-o','--log', help="File path for data logging in csv format. (Default: pm_co2_log.csv)", default="pm_co2_log.csv")
parser.add_argument('-i','--interval', help="Data logging interval in seconds. (Default: 1)", default=1, type=int)
parser.add_argument('-k','--key', help="API Key for pushing the data to Thingspeak platform. If not given, data will not be pushed to Thingspeak.")
args = parser.parse_args()

sds011 = serial.Serial(args.pm_port)
co2ndir = serial.Serial(args.co2_port)
os.system("echo 'time, pm2.5(ug/m3), pm10(ug/m3), co2(ppm)' >"+args.log)

# Inserted time delay to avoid race condition between serial port open and writing actions
time.sleep(2)

while True:
    co2ndir.write('ff0186000000000079'.decode('hex'))
    co2data = co2ndir.read(9)
    sds011.reset_input_buffer()
    pmdata = sds011.read(10)

    print(":".join("{:02x}".format(ord(c)) for c in co2data))
    print(":".join("{:02x}".format(ord(c)) for c in pmdata))

    if co2data[0].encode('hex') == "\xff".encode('hex') and co2data[1].encode('hex') == "\x86".encode('hex'):
        print("CO2: Message header valid")
        print("CO2: Command code valid")
    else:
        print("CO2: Invalid header bytes")
        continue
    
    if pmdata[0].encode('hex') == "\xaa".encode('hex') and pmdata[1].encode('hex') == "\xc0".encode('hex'):
        print("PM: Message header valid")
        print("PM: Commander number valid")
    else:
        print("PM: Invalid header bytes")
        continue
    
    co2ppm = ((int(co2data[2].encode('hex'),16)*256)+int(co2data[3].encode('hex'),16))
    pm25 = ((int(pmdata[3].encode('hex'),16)*256)+int(pmdata[2].encode('hex'),16))/10
    pm10 = ((int(pmdata[5].encode('hex'),16)*256)+int(pmdata[4].encode('hex'),16))/10

    print("{} >> PM2.5: {}, PM10: {}, CO2: {}".format(datetime.datetime.now(),pm25,pm10,co2ppm))
    os.system(("echo '{}, {}, {}, {}' >> "+args.log).format(str(datetime.datetime.now()),pm25,pm10,co2ppm))
 
    if(args.key):
    	try:
            urllib2.urlopen('https://api.thingspeak.com/update?api_key={}&field1={}&field2={}&field3={}'.format(args.key,pm25,pm10,co2ppm))
        except Exception:
            print("Thingspeak: An error occured!")
            pass
    print("")
    time.sleep(args.interval)

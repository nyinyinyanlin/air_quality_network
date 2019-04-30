import serial
import time

s = serial.Serial("/dev/ttyUSB1")
while True:
	s.write('ff0186000000000079'.decode('hex'))
	print(s.read(9))
	time.sleep(1)

# Quick logging tool to get debug info being sent over UDP to port

import argparse
import socket

parser = argparse.ArgumentParser(description='logging : python -u logging <udp-port>')
parser.add_argument('udpPort', type=int,help='UDP Port')
args = parser.parse_args()

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("", args.udpPort))

oldLog = ""

while True:
   data = sock.recv(2048)
   newLog = data.decode()
   if newLog != oldLog:
      print( f"{newLog}" )

   oldLog = newLog

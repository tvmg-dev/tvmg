# Quick logging tool to get debug info being sent over UDP to port
# 51003 on the subnet

import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("", 51003))

oldLog = ""

while True:
   data = sock.recv(2048)
   newLog = data.decode()
   if newLog != oldLog:
      print( f"{newLog}" )

   oldLog = newLog

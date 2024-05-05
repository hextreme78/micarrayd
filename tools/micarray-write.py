import sys
import getopt
import socket
import wave

opts, _ = getopt.getopt(sys.argv[1:], "i:", ["input=", "help"])
for opt, val in opts:
    if opt in ["-i", "--input"]:
        input = val
    elif opt == "--help":
        print("-i/--input input wave file")
        exit()

f = wave.open(input, "rb")
data = f.readframes(f.getnframes())
f.close()

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect("/var/run/micarrayd.socket")
sock.send(data)
sock.close()


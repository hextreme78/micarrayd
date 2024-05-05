import sys
import getopt
import socket
import wave

rate = 48000
channels = 2

def recv_all(sock, buffer_size):
    data = b""
    remaining_sz = buffer_size
    while remaining_sz:
        chunk = sock.recv(remaining_sz)
        data += chunk
        remaining_sz -= len(chunk)
    return data

opts, _ = getopt.getopt(sys.argv[1:], "r:c:d:o:f", ["rate=", "channels=", "duartion=", "output=", "fmt=", "help"])
for opt, val in opts:
    if opt in ["-r", "--rate"]:
        rate = int(val)
    elif opt in ["-c", "--channels"]:
        channels = int(val)
    elif opt in ["-d", "--duration"]:
        duration = float(val)
    elif opt in ["-o", "--output"]:
        output = val
    elif opt in ["-f", "--fmt"]:
        fmt = val
    elif opt == "--help":
        print("-r/--rate sampling rate")
        print("-c/--channels number of channels")
        print("-d/--duration duration of audio")
        print("-o/--output output file")
        exit()

sample_size = 2
nbytes = int(rate * duration * channels * sample_size)

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect("/var/run/micarrayd.socket")
data = recv_all(sock, nbytes)
sock.close()

f = wave.open(output, "wb")
f.setnchannels(channels)
f.setsampwidth(sample_size)
f.setframerate(rate)
f.writeframes(data)
f.close()


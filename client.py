import socket
import struct
import time

DEFAULT_HOST = 'localhost'
DEFAULT_PORT = 1883

SUBSCRIBE = 0x30
UNSUBSCRIBE = 0x40
PUBLISH = 0x20
PING = 0x50


class Client(object):
    def __init__(self):
        self.channel = 1;
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    def connect(self, host=DEFAULT_HOST, port=DEFAULT_PORT):
        self.socket.connect((host, port))

    def disconnect(self):
        self.socket.close()

    def subscribe(self, topic):
        data = struct.pack('!Hi%ds' % len(topic), SUBSCRIBE, len(topic), topic)
        self.socket.send(data)

    def unsubscribe(self, topic):
        data = struct.pack('!Hi%ds' % len(topic), UNSUBSCRIBE,
                           len(topic), topic)
        self.socket.send(data)

    def publish(self, topic, msg):
        size = len(topic) + len(msg) + 4
        data = struct.pack('!Hii%ds%ds' % (len(topic), len(msg)), PUBLISH,
                           size, len(topic), topic, msg)
        self.socket.send(data)
    
    def ping(self):
        ts = int(time.time() * 1000)
        data = struct.pack('!BIIQ' ,PING, 0x00000000, 0x00000008 ,ts)
        self.socket.send(data);

    def sub(self):
        data = struct.pack('!BIII' ,SUBSCRIBE, 0x00000000, 0x00000004 ,self.channel)
        self.socket.send(data);

    def unsub(self):
        data = struct.pack('!BIII' ,UNSUBSCRIBE, 0x00000000, 0x00000004 ,self.channel)
        self.socket.send(data);

    def pub(self):
        data = struct.pack('!BII8s' ,PUBLISH, 0x00000000, 0x00000008 ,"12345678")
        self.socket.send(data);

    def recv(self):
        b = self.socket.recv(25)
        print ''.join(x.encode('hex') for x in b)
 
if __name__ == "__main__":
    c = Client()
    c.connect()
    c.sub()
    for i in range(100000):
        c.ping()
        c.recv()
        c.pub()
        c.recv()


from digi.xbee.devices import XBeeDevice
from digi.xbee.exception import XBeeException
import os
import time

class XBeeService:
    def __init__(self):
        self.port = os.getenv("XBEE_PORT", "COM5")
        self.baud = int(os.getenv("XBEE_BAUD", "9600"))
        self.device = None

    def open(self):
        if self.device and self.device.is_open():
            return
        self.device = XBeeDevice(self.port, self.baud)
        self.device.open()

    def close(self):
        if self.device and self.device.is_open():
            self.device.close()

    def info(self):
        self.open()
        return {
            "port": self.port,
            "baud": self.baud,
            "64bit_addr": str(self.device.get_64bit_addr()),
            "node_id": self.device.get_node_id(),
        }

    def discover(self, timeout_s: int = 8):
        self.open()
        net = self.device.get_network()
        net.set_discovery_timeout(timeout_s)
        net.clear()
        devices = net.discover_devices()
        out = []
        if devices:
            for d in devices:
                out.append({
                    "node_id": d.get_node_id(),
                    "64bit_addr": str(d.get_64bit_addr()),
                })
        return out

    def send_broadcast(self, text: str):
        self.open()
        self.device.send_data_broadcast(text)
        return {"sent": True, "text": text, "ts": time.time()}
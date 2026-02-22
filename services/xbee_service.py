# services/xbee_service.py
import os
import threading
import time
from digi.xbee.devices import XBeeDevice

class XBeeService:

    def __init__(self):
        self.port = os.getenv("XBEE_PORT", "COM5") #change for every device
        self.baud = int(os.getenv("XBEE_BAUD", "9600"))
        self._lock = threading.Lock()
        self.device: XBeeDevice | None = None

        # ✅ in-memory RX buffer (for testing)
        self._rx = []  # list of dicts

    def open(self):
        with self._lock:
            if self.device and self.device.is_open():
                return
            self.device = XBeeDevice(self.port, self.baud)
            self.device.open()

            # ✅ attach callback once after opening
            self.device.add_data_received_callback(self._on_receive)

    def _on_receive(self, xbee_message):
        """Called automatically when RF data is received."""
        try:
            text = xbee_message.data.decode(errors="replace")
        except Exception:
            text = "<decode error>"

        item = {
            "text": text,
            "from_64bit": str(xbee_message.remote_device.get_64bit_addr()),
            "ts": time.time(),
        }

        self._rx.append(item)

        # keep buffer from growing forever
        if len(self._rx) > 200:
            self._rx = self._rx[-200:]

        print("XBee RX:", item)

    def close(self):
        with self._lock:
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

    def send_broadcast(self, text: str):
        self.open()
        self.device.send_data_broadcast(text)
        return {"sent": True, "text": text}

    # ✅ expose RX buffer for API
    def get_received(self):
        return list(self._rx)

    def clear_received(self):
        self._rx.clear()
        return {"cleared": True}
    

xbee_service = XBeeService()

def send_broadcast(text: str):
    return xbee_service.send_broadcast(text)

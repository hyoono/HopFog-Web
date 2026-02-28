# services/xbee_service.py
import json
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
        self._command_handler = None  # callback for parsed JSON commands

        # ✅ in-memory RX buffer (for testing)
        self._rx = []  # list of dicts

    def set_command_handler(self, handler):
        """Register a callback: handler(command_dict, from_64bit_addr_str)"""
        self._command_handler = handler

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

        from_addr = str(xbee_message.remote_device.get_64bit_addr())

        item = {
            "text": text,
            "from_64bit": from_addr,
            "ts": time.time(),
        }

        self._rx.append(item)

        # keep buffer from growing forever
        if len(self._rx) > 200:
            self._rx = self._rx[-200:]

        print("XBee RX:", item)

        # Parse as JSON command and dispatch to node protocol handler
        try:
            doc = json.loads(text)
            if isinstance(doc, dict) and "cmd" in doc:
                if self._command_handler:
                    self._command_handler(doc, from_addr)
        except (json.JSONDecodeError, ValueError):
            pass  # not a JSON command — ignore

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

    def send_json(self, data: dict):
        """Send a JSON-encoded command via broadcast."""
        text = json.dumps(data, separators=(",", ":"))
        return self.send_broadcast(text)

    # ✅ expose RX buffer for API
    def get_received(self):
        return list(self._rx)

    def clear_received(self):
        self._rx.clear()
        return {"cleared": True}
    

xbee_service = XBeeService()

def send_broadcast(text: str):
    return xbee_service.send_broadcast(text)

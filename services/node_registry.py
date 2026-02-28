# services/node_registry.py
"""In-memory tracking of connected HopFog nodes (heartbeats, stats)."""

import time
from typing import Dict, Optional


class NodeInfo:
    def __init__(self, node_id: str, params: dict, xbee_addr: str):
        self.node_id = node_id
        self.xbee_addr = xbee_addr
        self.ip_address = params.get("ip_address", "")
        self.device_name = params.get("device_name", node_id)
        self.status = "active"
        self.registered_at = time.time()
        self.last_heartbeat = time.time()
        self.stats: dict = {}

    def to_dict(self) -> dict:
        return {
            "node_id": self.node_id,
            "xbee_addr": self.xbee_addr,
            "ip_address": self.ip_address,
            "device_name": self.device_name,
            "status": self.status,
            "registered_at": self.registered_at,
            "last_heartbeat": self.last_heartbeat,
            "seconds_since_heartbeat": int(time.time() - self.last_heartbeat),
            "stats": self.stats,
        }


STALE_THRESHOLD_SECONDS = 90


class NodeRegistry:
    def __init__(self):
        self._nodes: Dict[str, NodeInfo] = {}

    def register(self, node_id: str, params: dict, xbee_addr: str):
        self._nodes[node_id] = NodeInfo(node_id, params, xbee_addr)

    def heartbeat(self, node_id: str, params: dict, xbee_addr: str):
        if node_id not in self._nodes:
            self.register(node_id, params, xbee_addr)
        node = self._nodes[node_id]
        node.last_heartbeat = time.time()
        node.status = "active"
        node.ip_address = params.get("ip_address", node.ip_address)

    def update_stats(self, node_id: str, stats: dict):
        if node_id in self._nodes:
            self._nodes[node_id].stats = stats

    def get_node(self, node_id: str) -> Optional[NodeInfo]:
        return self._nodes.get(node_id)

    def get_all(self) -> list:
        now = time.time()
        for n in self._nodes.values():
            if now - n.last_heartbeat > STALE_THRESHOLD_SECONDS:
                n.status = "stale"
        return [n.to_dict() for n in self._nodes.values()]

    def count_active(self) -> int:
        now = time.time()
        return sum(1 for n in self._nodes.values()
                   if now - n.last_heartbeat <= STALE_THRESHOLD_SECONDS)


node_registry = NodeRegistry()

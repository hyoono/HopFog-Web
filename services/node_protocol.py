# services/node_protocol.py
"""Handle all incoming XBee JSON commands from HopFog nodes."""

import json
import time

from database.connection import SessionLocal
from database.models import (
    User, FogDevice, ResidentAdminMessage, BroadcastMessage,
)
from services.xbee_service import xbee_service
from services.node_registry import node_registry


def handle_node_command(doc: dict, from_addr: str):
    """Dispatch an incoming XBee JSON command from a node."""
    cmd = doc.get("cmd", "")
    node_id = doc.get("node_id", "unknown")
    params = doc.get("params", {})

    handlers = {
        "REGISTER": _handle_register,
        "HEARTBEAT": _handle_heartbeat,
        "SYNC_REQUEST": _handle_sync_request,
        "RELAY_MSG": _handle_relay_msg,
        "RELAY_FOG_NODE": _handle_relay_fog_node,
        "RELAY_CHAT_MSG": _handle_relay_chat_msg,
        "SOS_ALERT": _handle_sos_alert,
        "CHANGE_PASSWORD": _handle_change_password,
        "STATS_RESPONSE": _handle_stats_response,
    }

    handler = handlers.get(cmd)
    if handler:
        try:
            handler(node_id, params, from_addr)
        except Exception as e:
            print(f"[NODE-PROTO] Error handling {cmd} from {node_id}: {e}")
    else:
        print(f"[NODE-PROTO] Unknown command from {node_id}: {cmd}")


def trigger_sync_request(node_id: str):
    """Public API: trigger a data sync to a specific node."""
    _handle_sync_request(node_id, {}, "")


def _handle_register(node_id, params, from_addr):
    """Node registered — store it and reply REGISTER_ACK."""
    node_registry.register(node_id, params, from_addr)

    db = SessionLocal()
    try:
        device_name = params.get("device_name", node_id)
        existing = db.query(FogDevice).filter(
            FogDevice.device_name == device_name
        ).first()
        if existing:
            existing.status = "active"
            db.commit()
        else:
            db.add(FogDevice(device_name=device_name, status="active"))
            db.commit()
    finally:
        db.close()

    xbee_service.send_json({"cmd": "REGISTER_ACK", "node_id": node_id})
    print(f"[NODE-PROTO] Registered node {node_id}")


def _handle_heartbeat(node_id, params, from_addr):
    """Node heartbeat — update status, reply PONG."""
    node_registry.heartbeat(node_id, params, from_addr)
    xbee_service.send_json({"cmd": "PONG", "node_id": node_id})


def _handle_sync_request(node_id, params, from_addr):
    """Node wants full data — send SYNC_DATA."""
    db = SessionLocal()
    try:
        users = db.query(User).filter(User.is_active == 1).all()
        users_data = [
            {
                "id": u.id,
                "username": u.username,
                "email": u.email,
                "role": u.role,
                "is_active": bool(u.is_active),
                "has_agreed_sos": bool(getattr(u, "has_agreed_sos", False)),
            }
            for u in users
        ]

        broadcasts = (
            db.query(BroadcastMessage)
            .filter(BroadcastMessage.status == "sent")
            .order_by(BroadcastMessage.created_at.desc())
            .limit(50)
            .all()
        )
        announcements_data = [
            {
                "id": b.id,
                "title": b.subject,
                "message": b.body,
                "created_at": str(int(b.created_at.timestamp())) if b.created_at else "0",
            }
            for b in broadcasts
        ]

        fog_devices = db.query(FogDevice).all()
        fog_nodes_data = [
            {
                "id": d.id,
                "device_name": d.device_name,
                "status": d.status,
            }
            for d in fog_devices
        ]

        sync_payload = {
            "cmd": "SYNC_DATA",
            "node_id": node_id,
            "users": users_data,
            "announcements": announcements_data,
            "conversations": [],
            "chat_messages": [],
            "fog_nodes": fog_nodes_data,
            "messages": [],
        }

        xbee_service.send_json(sync_payload)
        print(f"[NODE-PROTO] Sent SYNC_DATA to {node_id}")
    finally:
        db.close()


def _handle_relay_msg(node_id, params, from_addr):
    """Store a relayed user message."""
    print(f"[NODE-PROTO] Relay message from {node_id}: "
          f"{params.get('from')} -> {params.get('to')}")


def _handle_relay_fog_node(node_id, params, from_addr):
    """Register a fog device reported by a node."""
    db = SessionLocal()
    try:
        name = params.get("device_name", "")
        if not name:
            return
        existing = db.query(FogDevice).filter(
            FogDevice.device_name == name
        ).first()
        if existing:
            existing.status = params.get("status", "active")
            db.commit()
        else:
            db.add(FogDevice(
                device_name=name,
                status=params.get("status", "active"),
            ))
            db.commit()
    finally:
        db.close()


def _handle_relay_chat_msg(node_id, params, from_addr):
    """Store a chat message relayed from a node's mobile user."""
    print(f"[NODE-PROTO] Chat message from node {node_id}: "
          f"conv={params.get('conversation_id')} "
          f"sender={params.get('sender_id')}")


def _handle_sos_alert(node_id, params, from_addr):
    """Create an SOS request from a mobile user via a node."""
    user_id = params.get("user_id", 0)
    db = SessionLocal()
    try:
        user = db.query(User).filter(User.id == user_id).first()
        username = user.username if user else f"User {user_id}"
        sos = ResidentAdminMessage(
            sender_id=user_id,
            kind="sos_request",
            subject=f"SOS from {username} (via node {node_id})",
            body=f"SOS alert received via fog node {node_id}",
            priority=100,
            status="queued",
        )
        db.add(sos)
        db.commit()
        print(f"[NODE-PROTO] SOS alert created for user {user_id} via {node_id}")
    finally:
        db.close()


def _handle_change_password(node_id, params, from_addr):
    """Change a user's password (relayed from node)."""
    user_id = params.get("user_id", 0)
    new_password = params.get("new_password", "")
    if not user_id or not new_password:
        return
    db = SessionLocal()
    try:
        user = db.query(User).filter(User.id == user_id).first()
        if user:
            from routes.auth import get_password_hash
            user.password_hash = get_password_hash(new_password)
            db.commit()
            print(f"[NODE-PROTO] Password changed for user {user_id}")
    finally:
        db.close()


def _handle_stats_response(node_id, params, from_addr):
    """Store stats reported by a node."""
    node_registry.update_stats(node_id, params)

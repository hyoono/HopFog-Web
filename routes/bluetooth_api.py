from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field
from typing import Dict, Any, Optional
import time
import uuid

router = APIRouter(prefix="/api/bluetooth", tags=["bluetooth"])

# In-memory queue (temporary placeholder)
BT_OUTBOX: Dict[str, Dict[str, Any]] = {}

class BluetoothSendRequest(BaseModel):
    fog_id: str
    channel: str
    payload: Dict[str, Any]
    priority: int = 5

class BluetoothAckRequest(BaseModel):
    bt_msg_id: str
    status: str
    detail: Optional[str] = None

@router.post("/send")
def bluetooth_send(req: BluetoothSendRequest):
    bt_msg_id = f"bt_{uuid.uuid4().hex}"
    now = time.time()

    BT_OUTBOX[bt_msg_id] = {
        "bt_msg_id": bt_msg_id,
        "fog_id": req.fog_id,
        "channel": req.channel,
        "payload": req.payload,
        "priority": req.priority,
        "status": "queued",
        "queued_at": now,
        "updated_at": now,
    }

    return {
        "bt_msg_id": bt_msg_id,
        "status": "queued",
        "queued_at": now
    }

@router.get("/outbox/next")
def bluetooth_outbox_next(fog_id: str):
    candidates = [
        v for v in BT_OUTBOX.values()
        if v["fog_id"] == fog_id and v["status"] == "queued"
    ]

    if not candidates:
        return {"message": None}

    candidates.sort(key=lambda x: (x["priority"], x["queued_at"]))
    msg = candidates[0]

    msg["status"] = "sent"
    msg["updated_at"] = time.time()

    return {"message": msg}

@router.post("/ack")
def bluetooth_ack(req: BluetoothAckRequest):
    msg = BT_OUTBOX.get(req.bt_msg_id)
    if not msg:
        raise HTTPException(status_code=404, detail="Message not found")

    msg["status"] = req.status
    msg["updated_at"] = time.time()
    if req.detail:
        msg["detail"] = req.detail

    return msg

@router.get("/status/{bt_msg_id}")
def bluetooth_status(bt_msg_id: str):
    msg = BT_OUTBOX.get(bt_msg_id)
    if not msg:
        raise HTTPException(status_code=404, detail="Message not found")
    return msg

@router.get("/outbox/list")
def bluetooth_outbox_list(fog_id: str | None = None, status: str | None = None):
    """
    Debug endpoint: list all messages currently in BT_OUTBOX.
    Optional filters: fog_id, status (queued/sent/acked/failed/etc.)
    """
    items = list(BT_OUTBOX.values())

    if fog_id:
        items = [m for m in items if m.get("fog_id") == fog_id]
    if status:
        items = [m for m in items if m.get("status") == status]

    # Show newest first for convenience
    items.sort(key=lambda x: x.get("queued_at", 0), reverse=True)
    return {"count": len(items), "messages": items}

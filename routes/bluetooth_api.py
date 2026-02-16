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


# -----------------------------
# Resident Bluetooth Outbox (DB-backed)
# -----------------------------
from datetime import datetime, timezone
from sqlalchemy.orm import Session
from sqlalchemy import asc, desc
from database.deps import get_db
from database.models import (
    BroadcastMessage, BroadcastRecipient,
    Message, MessageRecipient
)
from fastapi import Depends, Query


@router.get("/resident/outbox/next")
def resident_outbox_next(
    user_id: int = Query(..., description="Resident user id"),
    db: Session = Depends(get_db),
):
    """
    Returns the next pending item for a resident device connected via Bluetooth.

    Order (minimal, demo-ready):
      1) Pending private messages addressed to this user (MessageRecipient.status == 'sent')
      2) Pending admin broadcasts for this user (BroadcastRecipient.status == 'queued')

    NOTE: This does not replace the existing fog_id BT_OUTBOX placeholder endpoints.
    """

    # 1) Private messages first
    pm_row = (
        db.query(MessageRecipient, Message)
        .join(Message, Message.id == MessageRecipient.message_id)
        .filter(MessageRecipient.user_id == user_id)
        .filter(MessageRecipient.status == "sent")
        .order_by(desc(Message.created_at))
        .first()
    )
    if pm_row:
        mr, msg = pm_row
        # Mark as "delivered" only after /resident/ack; here we can mark "sent_to_device"
        # but MessageRecipient schema only has sent/delivered/read, so keep as sent.
        return {
            "message": {
                "type": "private",
                "id": msg.id,
                "recipient_row": {"message_id": mr.message_id, "user_id": mr.user_id},
                "subject": msg.subject,
                "body": msg.body,
                "created_at": msg.created_at.isoformat() if msg.created_at else None,
            }
        }

    # 2) Broadcasts not yet delivered to this user
    br_row = (
        db.query(BroadcastRecipient, BroadcastMessage)
        .join(BroadcastMessage, BroadcastMessage.id == BroadcastRecipient.broadcast_id)
        .filter(BroadcastRecipient.user_id == user_id)
        .filter(BroadcastRecipient.status == "queued")
        .order_by(asc(BroadcastMessage.priority), desc(BroadcastMessage.created_at))
        .first()
    )
    if br_row:
        br, bm = br_row
        # Mark as sent attempt
        br.status = "sent"
        br.attempts = (br.attempts or 0) + 1
        br.last_attempt_at = datetime.now(timezone.utc)
        br.sent_at = datetime.now(timezone.utc)
        db.commit()

        return {
            "message": {
                "type": "broadcast",
                "id": bm.id,
                "recipient_row": {"broadcast_id": br.broadcast_id, "user_id": br.user_id},
                "msg_type": bm.msg_type,
                "severity": bm.severity,
                "audience": bm.audience,
                "subject": bm.subject,
                "body": bm.body,
                "priority": bm.priority,
                "created_at": bm.created_at.isoformat() if bm.created_at else None,
            }
        }

    return {"message": None}


class ResidentBluetoothAckRequest(BaseModel):
    user_id: int
    item_type: str = Field(..., description="private | broadcast")
    item_id: int = Field(..., description="Message.id for private; BroadcastMessage.id for broadcast")
    status: str = Field(..., description="delivered | read | failed")
    detail: Optional[str] = None


@router.post("/resident/ack")
def resident_ack(req: ResidentBluetoothAckRequest, db: Session = Depends(get_db)):
    """
    Resident device ACK for items received over Bluetooth.

    - private: updates MessageRecipient.status to delivered/read
    - broadcast: updates BroadcastRecipient.status to delivered/read/failed
    """
    now = datetime.now(timezone.utc)

    if req.item_type == "private":
        mr = (
            db.query(MessageRecipient)
            .filter(MessageRecipient.user_id == req.user_id)
            .filter(MessageRecipient.message_id == req.item_id)
            .first()
        )
        if not mr:
            raise HTTPException(status_code=404, detail="Private recipient row not found")

        if req.status in ("delivered", "read"):
            mr.status = req.status
            if req.status == "read":
                mr.read_at = now
            db.commit()
            return {"ok": True, "type": "private", "message_id": req.item_id, "status": mr.status}

        if req.status == "failed":
            # keep as 'sent' so it can be retried next poll
            return {"ok": True, "type": "private", "message_id": req.item_id, "status": mr.status, "detail": req.detail}

        raise HTTPException(status_code=400, detail="Invalid status for private")

    if req.item_type == "broadcast":
        br = (
            db.query(BroadcastRecipient)
            .filter(BroadcastRecipient.user_id == req.user_id)
            .filter(BroadcastRecipient.broadcast_id == req.item_id)
            .first()
        )
        if not br:
            raise HTTPException(status_code=404, detail="Broadcast recipient row not found")

        if req.status == "delivered":
            br.status = "delivered"
            br.delivered_at = now
        elif req.status == "read":
            br.status = "read"
            br.read_at = now
        elif req.status == "failed":
            br.status = "failed"
            br.fail_reason = (req.detail or "bluetooth delivery failed")[:255]
        else:
            raise HTTPException(status_code=400, detail="Invalid status for broadcast")

        db.commit()
        return {"ok": True, "type": "broadcast", "broadcast_id": req.item_id, "status": br.status}

    raise HTTPException(status_code=400, detail="item_type must be 'private' or 'broadcast'")

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel
from sqlalchemy.orm import Session
from typing import Optional, List

from database.deps import get_db
from database.models import ResidentAdminMessage, User

router = APIRouter(prefix="/api/resident-admin", tags=["Resident -> Admin"])

# Priority mapping (tuned for HopFog)
PRIORITY_NORMAL = 30
PRIORITY_SOS_REQUEST = 90

class ResidentAdminCreate(BaseModel):
    sender_id: int
    kind: str = "message"  # message | sos_request
    subject: Optional[str] = None
    body: str

@router.post("/messages")
def create_resident_admin_message(payload: ResidentAdminCreate, db: Session = Depends(get_db)):
    sender = db.query(User).filter(User.id == payload.sender_id).first()
    if not sender:
        raise HTTPException(status_code=404, detail="Sender not found")

    kind = (payload.kind or "message").lower().strip()
    if kind not in {"message", "sos_request"}:
        kind = "message"

    priority = PRIORITY_SOS_REQUEST if kind == "sos_request" else PRIORITY_NORMAL

    m = ResidentAdminMessage(
        sender_id=payload.sender_id,
        kind=kind,
        subject=(payload.subject.strip() if payload.subject else None),
        body=payload.body.strip(),
        priority=priority,
        status="queued",
    )
    db.add(m)
    db.commit()
    db.refresh(m)

    return {"id": m.id, "status": m.status, "priority": m.priority}

@router.get("/messages")
def list_resident_admin_messages(
    kind: Optional[str] = None,
    status: Optional[str] = None,
    limit: int = 100,
    db: Session = Depends(get_db),
):
    q = db.query(ResidentAdminMessage)
    if kind:
        q = q.filter(ResidentAdminMessage.kind == kind.lower())
    if status:
        q = q.filter(ResidentAdminMessage.status == status.lower())

    rows = q.order_by(ResidentAdminMessage.priority.desc(), ResidentAdminMessage.created_at.desc()).limit(limit).all()
    return [
        {
            "id": r.id,
            "sender_id": r.sender_id,
            "kind": r.kind,
            "subject": r.subject,
            "body": r.body,
            "priority": r.priority,
            "status": r.status,
            "admin_action": r.admin_action,
            "created_at": str(r.created_at),
            "handled_by": r.handled_by,
            "handled_at": str(r.handled_at) if r.handled_at else None,
        }
        for r in rows
    ]

class ResidentAdminStatusUpdate(BaseModel):
    status: str
    admin_action: Optional[str] = None
    handled_by: Optional[int] = None

@router.patch("/messages/{message_id}")
def update_resident_admin_message(message_id: int, payload: ResidentAdminStatusUpdate, db: Session = Depends(get_db)):
    r = db.query(ResidentAdminMessage).filter(ResidentAdminMessage.id == message_id).first()
    if not r:
        raise HTTPException(status_code=404, detail="Message not found")

    r.status = (payload.status or r.status).lower().strip()
    if payload.admin_action is not None:
        r.admin_action = payload.admin_action.lower().strip()
    if payload.handled_by is not None:
        r.handled_by = payload.handled_by
    db.commit()
    return {"ok": True}

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session
from sqlalchemy import or_, and_
from pydantic import BaseModel
from typing import List, Optional

from database.deps import get_db
from database.models import Message, MessageRecipient, User
from routes.auth import verify_token

router = APIRouter(prefix="/api/messages", tags=["Messages"])

class MessageCreate(BaseModel):
    sender_id: int
    subject: Optional[str] = None
    body: str
    recipient_ids: List[int]

@router.post("")
def send_message(payload: MessageCreate, db: Session = Depends(get_db)):
    sender = db.query(User).filter(User.id == payload.sender_id).first()
    if not sender:
        raise HTTPException(status_code=404, detail="Sender not found")

    recipients = db.query(User).filter(User.id.in_(payload.recipient_ids)).all()
    if len(recipients) != len(set(payload.recipient_ids)):
        raise HTTPException(status_code=400, detail="One or more recipients not found")

    msg = Message(sender_id=payload.sender_id, subject=payload.subject, body=payload.body)
    db.add(msg)
    db.commit()
    db.refresh(msg)

    for rid in payload.recipient_ids:
        db.add(MessageRecipient(message_id=msg.id, user_id=rid, status="sent"))

    db.commit()
    return {"message_id": msg.id, "sent_to": payload.recipient_ids}

@router.get("/inbox/{user_id}")
def inbox(user_id: int, db: Session = Depends(get_db)):
    rows = (
        db.query(MessageRecipient, Message)
        .join(Message, Message.id == MessageRecipient.message_id)
        .filter(MessageRecipient.user_id == user_id)
        .order_by(Message.created_at.desc())
        .all()
    )

    return [
        {
            "message_id": m.id,
            "from_user_id": m.sender_id,
            "subject": m.subject,
            "body": m.body,
            "status": mr.status,
            "created_at": str(m.created_at),
            "read_at": str(mr.read_at) if mr.read_at else None,
        }
        for (mr, m) in rows
    ]

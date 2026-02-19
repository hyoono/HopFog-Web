from __future__ import annotations

from datetime import datetime, timedelta, timezone
from typing import Optional

from fastapi import APIRouter, Depends, Form, HTTPException, Request
from fastapi.responses import HTMLResponse, RedirectResponse
from fastapi.templating import Jinja2Templates
from sqlalchemy.orm import Session
from sqlalchemy import func, desc

from database.deps import get_db
from database.models import (
    User, Role, UserRole,
    BroadcastMessage, BroadcastRecipient, BroadcastEvent, ResidentAdminMessage
)
from routes.auth import verify_token
from services.broadcast_dispatcher import dispatcher

templates = Jinja2Templates(directory="templates")

router = APIRouter(prefix="/admin/messaging", tags=["Admin Messaging UI"])


def _is_admin(db: Session, user_id: int) -> bool:
    # Dev-friendly: if roles table is empty or 'admin' role doesn't exist yet, allow.
    admin_role = db.query(Role).filter(func.lower(Role.name) == "admin").first()
    if not admin_role:
        return True
    link = db.query(UserRole).filter(UserRole.user_id == user_id, UserRole.role_id == admin_role.id).first()
    return link is not None


def _get_residents(db: Session) -> list[User]:
    # Prefer explicit 'resident' role if present; otherwise return all active non-admin users.
    resident_role = db.query(Role).filter(func.lower(Role.name) == "resident").first()
    if resident_role:
        return (
            db.query(User)
            .join(UserRole, UserRole.user_id == User.id)
            .filter(UserRole.role_id == resident_role.id)
            .filter(User.is_active == 1)
            .order_by(User.id.asc())
            .all()
        )

    # Fallback: active users who are not admins (or if no admin role exists, then all active)
    admin_role = db.query(Role).filter(func.lower(Role.name) == "admin").first()
    if not admin_role:
        return db.query(User).filter(User.is_active == 1).order_by(User.id.asc()).all()

    admin_user_ids = {ur.user_id for ur in db.query(UserRole).filter(UserRole.role_id == admin_role.id).all()}
    return (
        db.query(User)
        .filter(User.is_active == 1)
        .filter(~User.id.in_(admin_user_ids))
        .order_by(User.id.asc())
        .all()
    )


def _priority_for(msg_type: str) -> int:
    t = (msg_type or "").lower()
    if t == "sos":
        return 100
    if t == "alert":
        return 50
    return 10


def _require_admin(db: Session, current_user: User):
    if not _is_admin(db, current_user.id):
        raise HTTPException(status_code=403, detail="Admins only")


@router.get("", response_class=HTMLResponse)
def overview(
    request: Request,
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token),
):
    _require_admin(db, current_user)

    total = db.query(BroadcastMessage).count()
    queued = db.query(BroadcastMessage).filter(BroadcastMessage.status == "queued").count()
    drafts = db.query(BroadcastMessage).filter(BroadcastMessage.status == "draft").count()
    recent_sos = (
        db.query(BroadcastMessage)
        .filter(BroadcastMessage.msg_type == "sos")
        .order_by(desc(BroadcastMessage.created_at))
        .limit(5)
        .all()
    )

    return templates.TemplateResponse("admin_messaging_overview.html", {
        "request": request,
        "current_user": current_user,
        "total": total,
        "queued": queued,
        "drafts": drafts,
        "recent_sos": recent_sos,
    })


@router.get("/broadcasts", response_class=HTMLResponse)
def broadcasts_page(
    request: Request,
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token),
    success: Optional[str] = None,
    error: Optional[str] = None,
):
    _require_admin(db, current_user)

    broadcasts = (
        db.query(BroadcastMessage)
        .order_by(desc(BroadcastMessage.created_at))
        .limit(100)
        .all()
    )

    residents = _get_residents(db)

    return templates.TemplateResponse("admin_broadcasts.html", {
        "request": request,
        "current_user": current_user,
        "broadcasts": broadcasts,
        "resident_count": len(residents),
        "success": success,
        "error": error,
    })


@router.post("/broadcasts")
def create_broadcast(
    request: Request,
    msg_type: str = Form("announcement"),
    severity: str = Form("info"),
    subject: str = Form(...),
    body: str = Form(...),
    ttl_hours: int = Form(24),
    action: str = Form("draft"),  # draft or queue
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token),
):
    _require_admin(db, current_user)

    msg_type = (msg_type or "announcement").lower()
    severity = (severity or "info").lower()
    action = (action or "draft").lower()

    if msg_type not in {"announcement", "alert", "sos"}:
        msg_type = "announcement"
    if severity not in {"info", "warning", "critical"}:
        severity = "info"
    if ttl_hours < 1:
        ttl_hours = 1
    if ttl_hours > 24 * 30:
        ttl_hours = 24 * 30

    ttl_expires_at = datetime.now(timezone.utc) + timedelta(hours=ttl_hours)

    status = "draft" if action == "draft" else "queued"
    priority = _priority_for(msg_type)

    b = BroadcastMessage(
        created_by=current_user.id,
        msg_type=msg_type,
        severity=severity,
        audience="all_residents",
        subject=subject.strip(),
        body=body.strip(),
        status=status,
        priority=priority,
        ttl_expires_at=ttl_expires_at,
    )
    db.add(b)
    db.commit()
    db.refresh(b)

    db.add(BroadcastEvent(broadcast_id=b.id, event_type="created", message=f"Created as {status.upper()}"))
    if status == "queued":
        db.add(BroadcastEvent(broadcast_id=b.id, event_type="queued", message="Queued for dispatch"))
    db.commit()

    # Pre-create recipients for tracking.
    residents = _get_residents(db)
    for u in residents:
        db.add(BroadcastRecipient(broadcast_id=b.id, user_id=u.id, status=("queued" if status == "queued" else "queued")))
    db.commit()

    # Priority logic:
    # - If queued SOS, dispatch immediately.
    # - Otherwise, wake the dispatcher so it can pick up newly queued items ASAP.
    if status == "queued":
        if msg_type == "sos":
            dispatcher.dispatch_now(b.id)
        else:
            dispatcher.wake()

    return RedirectResponse(url="/admin/messaging/broadcasts?success=Broadcast%20created", status_code=303)


@router.get("/broadcasts/{broadcast_id}", response_class=HTMLResponse)
def broadcast_detail(
    broadcast_id: int,
    request: Request,
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token),
    success: Optional[str] = None,
    error: Optional[str] = None,
):
    _require_admin(db, current_user)

    b = db.query(BroadcastMessage).filter(BroadcastMessage.id == broadcast_id).first()
    if not b:
        raise HTTPException(status_code=404, detail="Broadcast not found")

    # Summary counts
    status_counts = dict(
        db.query(BroadcastRecipient.status, func.count(BroadcastRecipient.id))
        .filter(BroadcastRecipient.broadcast_id == broadcast_id)
        .group_by(BroadcastRecipient.status)
        .all()
    )
    total = sum(status_counts.values()) if status_counts else 0

    events = (
        db.query(BroadcastEvent)
        .filter(BroadcastEvent.broadcast_id == broadcast_id)
        .order_by(desc(BroadcastEvent.created_at))
        .limit(50)
        .all()
    )

    return templates.TemplateResponse("admin_broadcast_detail.html", {
        "request": request,
        "current_user": current_user,
        "b": b,
        "status_counts": status_counts,
        "total": total,
        "events": events,
        "success": success,
        "error": error,
    })


@router.post("/broadcasts/{broadcast_id}/mark_sent")
def mark_sent(
    broadcast_id: int,
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token),
):
    _require_admin(db, current_user)

    b = db.query(BroadcastMessage).filter(BroadcastMessage.id == broadcast_id).first()
    if not b:
        raise HTTPException(status_code=404, detail="Broadcast not found")

    b.status = "sent"
    # Mark all recipients as sent (simulation)
    now = datetime.now(timezone.utc)
    db.query(BroadcastRecipient).filter(BroadcastRecipient.broadcast_id == broadcast_id).update(
        {"status": "sent", "sent_at": now},
        synchronize_session=False,
    )
    db.add(BroadcastEvent(broadcast_id=b.id, event_type="marked_sent", message="Marked as SENT (simulation)"))
    db.commit()

    return RedirectResponse(url=f"/admin/messaging/broadcasts/{broadcast_id}?success=Marked%20as%20sent", status_code=303)


@router.post("/broadcasts/{broadcast_id}/cancel")
def cancel_broadcast(
    broadcast_id: int,
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token),
):
    _require_admin(db, current_user)

    b = db.query(BroadcastMessage).filter(BroadcastMessage.id == broadcast_id).first()
    if not b:
        raise HTTPException(status_code=404, detail="Broadcast not found")

    b.status = "cancelled"
    db.add(BroadcastEvent(broadcast_id=b.id, event_type="cancelled", message="Cancelled by admin"))
    db.commit()

    return RedirectResponse(url=f"/admin/messaging/broadcasts/{broadcast_id}?success=Cancelled", status_code=303)


@router.get("/sos", response_class=HTMLResponse)
def sos_console(
    request: Request,
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token),
):
    _require_admin(db, current_user)

    sos_list = (
        db.query(BroadcastMessage)
        .filter(BroadcastMessage.msg_type == "sos")
        .order_by(desc(BroadcastMessage.created_at))
        .limit(50)
        .all()
    )

    incoming_sos_requests = (
        db.query(ResidentAdminMessage)
        .filter(ResidentAdminMessage.kind == "sos_request")
        .filter(ResidentAdminMessage.status.in_(["queued", "delivered", "read"]))
        .order_by(desc(ResidentAdminMessage.priority), desc(ResidentAdminMessage.created_at))
        .limit(100)
        .all()
    )

    return templates.TemplateResponse("admin_sos.html"), {
        "request": request,
        "current_user": current_user,
        "sos_list": sos_list,
        "incoming_sos_requests": incoming_sos_requests,
    }
@router.post("/sos-requests/{request_id}/escalate")
def escalate_sos_request(
    request_id: int,
    escalate_to: str = Form("sos"),  # sos | alert | announcement
    subject: Optional[str] = Form(None),
    body: Optional[str] = Form(None),
    ttl_hours: int = Form(24),
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token),
):
    """
    Resident -> Admin SOS request triage:
    - admin decides whether to broadcast it (announcement/alert/sos) or handle privately (handled endpoint can be added later)
    """
    _require_admin(db, current_user)

    r = db.query(ResidentAdminMessage).filter(ResidentAdminMessage.id == request_id).first()
    if not r:
        raise HTTPException(status_code=404, detail="SOS request not found")
    if r.kind != "sos_request":
        raise HTTPException(status_code=400, detail="Only SOS requests can be escalated")

    escalate_to = (escalate_to or "sos").lower().strip()
    if escalate_to not in {"sos", "alert", "announcement"}:
        escalate_to = "sos"

    if ttl_hours < 1:
        ttl_hours = 1
    if ttl_hours > 24 * 30:
        ttl_hours = 24 * 30

    # Prepare broadcast content (default: reuse resident content)
    b_subject = (subject.strip() if subject else (r.subject or "Resident SOS Request"))
    b_body = (body.strip() if body else r.body)

    severity = "info"
    if escalate_to == "alert":
        severity = "warning"
    if escalate_to == "sos":
        severity = "critical"

    ttl_expires_at = datetime.now(timezone.utc) + timedelta(hours=ttl_hours)
    status = "queued"
    priority = _priority_for(escalate_to)

    b = BroadcastMessage(
        created_by=current_user.id,
        msg_type=escalate_to,
        severity=severity,
        audience="all_residents",
        subject=b_subject,
        body=b_body,
        status=status,
        priority=priority,
        ttl_expires_at=ttl_expires_at,
    )
    db.add(b)
    db.commit()
    db.refresh(b)

    db.add(BroadcastEvent(broadcast_id=b.id, event_type="created", message=f"Created from SOS request #{r.id}"))
    db.add(BroadcastEvent(broadcast_id=b.id, event_type="queued", message="Queued for dispatch"))
    db.commit()

    # Pre-create recipients for tracking
    residents = _get_residents(db)
    for u in residents:
        db.add(BroadcastRecipient(broadcast_id=b.id, user_id=u.id, status="queued"))
    db.commit()

    # Mark SOS request handled
    r.status = "resolved"
    r.admin_action = f"escalated_to_{escalate_to}"
    r.handled_by = current_user.id
    r.handled_at = datetime.now(timezone.utc)
    db.commit()

    # Dispatch priority logic (only admin broadcast SOS bypasses normal waiting)
    if escalate_to == "sos":
        dispatcher.dispatch_now(b.id)
    else:
        dispatcher.wake()

    return RedirectResponse(url="/admin/messaging/sos?success=Escalated", status_code=303)


@router.post("/sos-requests/{request_id}/dismiss")
def dismiss_sos_request(
    request_id: int,
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token),
):
    _require_admin(db, current_user)

    r = db.query(ResidentAdminMessage).filter(ResidentAdminMessage.id == request_id).first()
    if not r:
        raise HTTPException(status_code=404, detail="SOS request not found")

    r.status = "dismissed"
    r.admin_action = "handled_privately"
    r.handled_by = current_user.id
    r.handled_at = datetime.now(timezone.utc)
    db.commit()

    return RedirectResponse(url="/admin/messaging/sos?success=Dismissed", status_code=303)


    


@router.get("/queue", response_class=HTMLResponse)
def queue_monitor(
    request: Request,
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token),
):
    _require_admin(db, current_user)

    queued_items = (
        db.query(BroadcastMessage)
        .filter(BroadcastMessage.status == "queued")
        .order_by(desc(BroadcastMessage.priority), BroadcastMessage.created_at.asc())
        .limit(200)
        .all()
    )
    drafts = (
        db.query(BroadcastMessage)
        .filter(BroadcastMessage.status == "draft")
        .order_by(desc(BroadcastMessage.created_at))
        .limit(200)
        .all()
    )

    return templates.TemplateResponse("admin_queue.html", {
        "request": request,
        "current_user": current_user,
        "queued_items": queued_items,
        "drafts": drafts,
    })


@router.get("/tracking", response_class=HTMLResponse)
def tracking(
    request: Request,
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token),
):
    _require_admin(db, current_user)

    broadcasts = (
        db.query(BroadcastMessage)
        .order_by(desc(BroadcastMessage.created_at))
        .limit(100)
        .all()
    )

    # Build summary per broadcast
    summaries = []
    for b in broadcasts:
        counts = dict(
            db.query(BroadcastRecipient.status, func.count(BroadcastRecipient.id))
            .filter(BroadcastRecipient.broadcast_id == b.id)
            .group_by(BroadcastRecipient.status)
            .all()
        )
        total = sum(counts.values()) if counts else 0
        summaries.append({
            "b": b,
            "total": total,
            "sent": counts.get("sent", 0),
            "delivered": counts.get("delivered", 0),
            "read": counts.get("read", 0),
            "failed": counts.get("failed", 0),
            "queued": counts.get("queued", 0),
        })

    return templates.TemplateResponse("admin_tracking.html", {
        "request": request,
        "current_user": current_user,
        "summaries": summaries,
    })


@router.get("/testing", response_class=HTMLResponse)
def testing(
    request: Request,
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token),
):
    _require_admin(db, current_user)

    return templates.TemplateResponse("admin_testing.html", {
        "request": request,
        "current_user": current_user,
    })

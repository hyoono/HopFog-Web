"""Priority-based dispatcher for admin broadcast messages.

This worker polls the database for queued BroadcastMessage rows and sends them
via the XBeeService. Highest priority wins (SOS > Alert > Announcement).

Design goals:
- Deterministic ordering: priority DESC, created_at ASC
- SOS can be dispatched immediately (bypassing the poll interval)
- Minimal coupling: the UI route only queues; dispatcher sends

NOTE: Delivery/read acknowledgements are not yet implemented on the mobile side,
so we currently mark recipients as SENT once the RF broadcast is emitted.
"""

from __future__ import annotations

import threading
import time
from datetime import datetime, timezone
from typing import Optional

from sqlalchemy import or_

from database.connection import SessionLocal
from database.models import BroadcastEvent, BroadcastMessage, BroadcastRecipient
from services.xbee_service import XBeeService


class BroadcastDispatcher:
    def __init__(self, poll_interval: float = 2.0):
        self.poll_interval = poll_interval
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._lock = threading.Lock()
        self._svc = XBeeService()

        # When something urgent happens (e.g., SOS), we can wake the poll loop.
        self._wakeup = threading.Event()

    def start(self):
        if self._thread and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        self._stop.set()
        self._wakeup.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=3)
        try:
            self._svc.close()
        except Exception:
            pass

    def wake(self):
        """Wake the worker early (used for SOS/immediate queue changes)."""
        self._wakeup.set()

    def dispatch_now(self, broadcast_id: int) -> None:
        """Best-effort immediate dispatch for a specific broadcast."""
        # Ensure only one dispatch at a time so we don't interleave RF sends.
        with self._lock:
            db = SessionLocal()
            try:
                b = db.query(BroadcastMessage).filter(BroadcastMessage.id == broadcast_id).first()
                if not b or b.status != "queued":
                    return
                self._dispatch_one(db, b)
            finally:
                db.close()

    def _run(self):
        while not self._stop.is_set():
            try:
                # Wait, but allow early wakeups.
                self._wakeup.wait(timeout=self.poll_interval)
                self._wakeup.clear()

                with self._lock:
                    db = SessionLocal()
                    try:
                        b = self._get_next_queued(db)
                        if b:
                            self._dispatch_one(db, b)
                    finally:
                        db.close()
            except Exception:
                # Don't crash the whole web server because of a worker exception.
                time.sleep(self.poll_interval)

    def _get_next_queued(self, db):
        now = datetime.now(timezone.utc)
        return (
            db.query(BroadcastMessage)
            .filter(BroadcastMessage.status == "queued")
            # Skip expired broadcasts (if ttl_expires_at is set)
            .filter(or_(BroadcastMessage.ttl_expires_at.is_(None), BroadcastMessage.ttl_expires_at > now))
            .order_by(BroadcastMessage.priority.desc(), BroadcastMessage.created_at.asc())
            .first()
        )

    def _format_payload(self, b: BroadcastMessage) -> str:
        # Keep it compact so it's friendlier to RF packet size.
        tag = (b.msg_type or "announcement").upper()
        sev = (b.severity or "info").upper()
        subject = (b.subject or "").strip()
        body = (b.body or "").strip()
        if subject and body:
            return f"[{tag}|{sev}] {subject} - {body}"
        return f"[{tag}|{sev}] {subject or body}"

    def _dispatch_one(self, db, b: BroadcastMessage) -> None:
        payload = self._format_payload(b)
        now = datetime.now(timezone.utc)

        try:
            self._svc.send_broadcast(payload)

            b.status = "sent"
            db.query(BroadcastRecipient).filter(BroadcastRecipient.broadcast_id == b.id).update(
                {"status": "sent", "sent_at": now},
                synchronize_session=False,
            )
            db.add(BroadcastEvent(broadcast_id=b.id, event_type="sent", message="RF broadcast sent via XBee"))
            db.commit()
        except Exception as e:
            b.status = "failed"
            err = str(e)[:250]
            db.add(BroadcastEvent(broadcast_id=b.id, event_type="failed", message=err))
            db.query(BroadcastRecipient).filter(BroadcastRecipient.broadcast_id == b.id).update(
                {"status": "failed", "fail_reason": err},
                synchronize_session=False,
            )
            db.commit()


# Single shared dispatcher for the whole app.
dispatcher = BroadcastDispatcher(poll_interval=2.0)

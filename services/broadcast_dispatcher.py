import asyncio
import json
from sqlalchemy.orm import Session
from database.connection import SessionLocal
from database.models import BroadcastMessage
from services.xbee_service import send_broadcast


class BroadcastDispatcher:
    def __init__(self):
        self.running = False
        self._task = None

    def wake(self):
        # Called from sync routes to trigger immediate processing
        print("🔔 Dispatcher wake() called")
        self.process_queue()

    def dispatch_now(self, broadcast_id: int):
        db: Session = SessionLocal()
        try:
            b = (
                db.query(BroadcastMessage)
                .filter(BroadcastMessage.id == broadcast_id)
                .first()
            )
            if not b:
                print(f"dispatch_now: broadcast {broadcast_id} not found")
                return

            if b.status != "queued":
                print(f"dispatch_now: broadcast {broadcast_id} status is {b.status}, skipping")
                return

            print(f"⚡ dispatch_now: Dispatching broadcast ID {b.id}")

            try:
                payload = json.dumps({
                    "cmd": "BROADCAST_MSG",
                    "params": {
                        "from": "admin",
                        "to": "all",
                        "message": b.body or b.subject,
                        "subject": b.subject,
                        "msg_type": b.msg_type,
                        "severity": b.severity,
                    }
                }, separators=(",", ":"))
                send_broadcast(payload)
                b.status = "sent"
                db.commit()
                print("✅ dispatch_now: sent")
            except Exception as e:
                print("❌ dispatch_now: XBee send failed:", e)
                b.status = "failed"
                db.commit()
        finally:
            db.close()

    async def start(self):
        self.running = True
        print("🚀 Broadcast Dispatcher started")
        self._task = asyncio.create_task(self.loop())

    async def stop(self):
        print("🛑 Stopping Broadcast Dispatcher...")
        self.running = False
        if self._task:
            await self._task

    async def loop(self):
        while self.running:
            await asyncio.sleep(1)
            self.process_queue()

    def process_queue(self):
        db: Session = SessionLocal()
        print("Dispatcher checking for pending broadcasts...")

        try:
            broadcast = (
                db.query(BroadcastMessage)
                .filter(BroadcastMessage.status == "queued")
                .order_by(
                    BroadcastMessage.priority.desc(),
                    BroadcastMessage.created_at.asc()
                )
                .first()
            )

            if broadcast:
                print(f"📡 Dispatching broadcast ID {broadcast.id}")

                try:
                    payload = json.dumps({
                        "cmd": "BROADCAST_MSG",
                        "params": {
                            "from": "admin",
                            "to": "all",
                            "message": broadcast.body or broadcast.subject,
                            "subject": broadcast.subject,
                            "msg_type": broadcast.msg_type,
                            "severity": broadcast.severity,
                        }
                    }, separators=(",", ":"))
                    send_broadcast(payload)
                    broadcast.status = "sent"
                    db.commit()
                    print("✅ Broadcast sent successfully")

                except Exception as e:
                    print("❌ XBee send failed:", e)
                    broadcast.status = "failed"
                    db.commit()

        finally:
            db.close()


dispatcher = BroadcastDispatcher()
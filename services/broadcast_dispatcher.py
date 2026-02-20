import asyncio
from sqlalchemy.orm import Session
from database.connection import SessionLocal
from database.models import BroadcastMessage
from services.xbee_service import send_broadcast


class BroadcastDispatcher:
    def __init__(self):
        self.running = False

    async def start(self):
        self.running = True
        print("🚀 Broadcast Dispatcher started")
        asyncio.create_task(self.loop())

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
                    # 🔥 ACTUAL SEND
                    send_broadcast(broadcast.body or broadcast.subject)

                    broadcast.status = "sent"
                    db.commit()
                    print("✅ Broadcast sent successfully")

                except Exception as e:
                    print("❌ XBee send failed:", e)
                    broadcast.status = "FAILED"
                    db.commit()

        finally:
            db.close()


dispatcher = BroadcastDispatcher()
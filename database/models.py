from sqlalchemy import Column, Integer, String, Text, ForeignKey, DateTime, UniqueConstraint
from sqlalchemy.orm import relationship
from sqlalchemy.sql import func
from .connection import Base


class FogDevice(Base):
    __tablename__ = "fog_devices"
    
    id = Column(Integer, primary_key=True, index=True)
    device_name = Column(String(100), unique=True)
    status = Column(String(50), default="inactive")  
    storage_total = Column(String(50), nullable=True)
    storage_used = Column(String(50), nullable=True)
    connected_users = Column(Integer, default=0)

class FogMessage(Base):
    __tablename__ = "fog_messages"

    id = Column(Integer, primary_key=True, index=True)
    device_id = Column(Integer, ForeignKey("fog_devices.id"), nullable=False)
    direction = Column(String(20), nullable=False)  # incoming, outgoing
    message_type = Column(String(50), nullable=False)  # command, alert, data, status, sensor_data
    content = Column(Text, nullable=False)
    status = Column(String(20), default="sent")  # sent, delivered, received, failed
    created_at = Column(DateTime, server_default=func.now())

     # Relationship
    # device = relationship("FogDevice", back_populates="messages")

# ---------- AUTH / USERS ----------
class User(Base):
    __tablename__ = "users"

    id = Column(Integer, primary_key=True, index=True)
    email = Column(String(120), unique=True, index=True, nullable=False)
    username = Column(String(80), unique=True, nullable=True)
    password_hash = Column(String(255), nullable=False)  # store hash only
    is_active = Column(Integer, default=1, nullable=False)
    role = Column(String, default="mobile")  # "admin" or "mobile"
    has_agreed_sos = Column(Integer, default=0, nullable=False)
    created_at = Column(DateTime(timezone=True), server_default=func.now(), nullable=False)

    sent_messages = relationship("Message", back_populates="sender", foreign_keys="Message.sender_id", cascade="all, delete")
    received_messages = relationship("MessageRecipient", back_populates="user")

class Role(Base):
    __tablename__ = "roles"

    id = Column(Integer, primary_key=True)
    name = Column(String(50), unique=True, nullable=False)

class UserRole(Base):
    __tablename__ = "user_roles"
    user_id = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"), primary_key=True)
    role_id = Column(Integer, ForeignKey("roles.id", ondelete="CASCADE"), primary_key=True)

    __table_args__ = (UniqueConstraint("user_id", "role_id", name="uq_user_role"),)

# ---------- MESSAGING ----------
class Message(Base):
    __tablename__ = "messages"

    id = Column(Integer, primary_key=True, index=True)
    sender_id = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"), nullable=False)

    subject = Column(String(150), nullable=True)
    body = Column(Text, nullable=False)

    created_at = Column(DateTime(timezone=True), server_default=func.now(), nullable=False)

    sender = relationship("User", back_populates="sent_messages", foreign_keys=[sender_id])
    recipients = relationship("MessageRecipient", back_populates="message", cascade="all, delete")

class MessageRecipient(Base):
    __tablename__ = "message_recipients"

    message_id = Column(Integer, ForeignKey("messages.id", ondelete="CASCADE"), primary_key=True)
    user_id = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"), primary_key=True)

    status = Column(String(20), default="sent", nullable=False)  # sent/delivered/read
    read_at = Column(DateTime(timezone=True), nullable=True)

    message = relationship("Message", back_populates="recipients")
    user = relationship("User", back_populates="received_messages")




# ---------- RESIDENT -> ADMIN MESSAGING ----------
class ResidentAdminMessage(Base):
    __tablename__ = "resident_admin_messages"

    id = Column(Integer, primary_key=True, index=True)
    sender_id = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"), nullable=False, index=True)

    kind = Column(String(30), default="message", nullable=False)   # message / sos_request
    subject = Column(String(200), nullable=True)
    body = Column(Text, nullable=False)

    priority = Column(Integer, default=30, nullable=False)         # sos_request higher than normal message
    status = Column(String(30), default="queued", nullable=False)  # queued/delivered/read/resolved/dismissed

    admin_action = Column(String(40), default="none", nullable=False)  # none/escalated_to_alert/escalated_to_sos/escalated_to_announcement/handled_privately
    handled_by = Column(Integer, ForeignKey("users.id", ondelete="SET NULL"), nullable=True)
    handled_at = Column(DateTime(timezone=True), nullable=True)

    created_at = Column(DateTime(timezone=True), server_default=func.now(), nullable=False)
    updated_at = Column(DateTime(timezone=True), onupdate=func.now(), nullable=True)


# ---------- ADMIN BROADCAST MESSAGING ----------
class BroadcastMessage(Base):
    __tablename__ = "broadcast_messages"

    id = Column(Integer, primary_key=True, index=True)
    created_by = Column(Integer, ForeignKey("users.id", ondelete="SET NULL"), nullable=True)

    msg_type = Column(String(20), default="announcement", nullable=False)  # announcement/alert/sos
    severity = Column(String(20), default="info", nullable=False)         # info/warning/critical
    audience = Column(String(100), default="all_residents", nullable=False)

    subject = Column(String(200), nullable=False)
    body = Column(Text, nullable=False)

    status = Column(String(20), default="draft", nullable=False)  # draft/queued/sent/failed/cancelled
    priority = Column(Integer, default=10, nullable=False)        # SOS higher than alert higher than announcement
    ttl_expires_at = Column(DateTime(timezone=True), nullable=True)

    created_at = Column(DateTime(timezone=True), server_default=func.now(), nullable=False)
    updated_at = Column(DateTime(timezone=True), onupdate=func.now(), nullable=True)

    recipients = relationship("BroadcastRecipient", back_populates="broadcast", cascade="all, delete-orphan")
    events = relationship("BroadcastEvent", back_populates="broadcast", cascade="all, delete-orphan")


class BroadcastRecipient(Base):
    __tablename__ = "broadcast_recipients"
    __table_args__ = (
        UniqueConstraint("broadcast_id", "user_id", name="uq_broadcast_recipient"),
    )

    id = Column(Integer, primary_key=True, index=True)
    broadcast_id = Column(Integer, ForeignKey("broadcast_messages.id", ondelete="CASCADE"), nullable=False, index=True)
    user_id = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"), nullable=False, index=True)

    status = Column(String(20), default="queued", nullable=False)  # queued/sent/delivered/read/failed
    attempts = Column(Integer, default=0, nullable=False)
    last_attempt_at = Column(DateTime(timezone=True), nullable=True)

    sent_at = Column(DateTime(timezone=True), nullable=True)
    delivered_at = Column(DateTime(timezone=True), nullable=True)
    read_at = Column(DateTime(timezone=True), nullable=True)

    fail_reason = Column(String(255), nullable=True)

    broadcast = relationship("BroadcastMessage", back_populates="recipients")
    user = relationship("User")


class BroadcastEvent(Base):
    __tablename__ = "broadcast_events"

    id = Column(Integer, primary_key=True, index=True)
    broadcast_id = Column(Integer, ForeignKey("broadcast_messages.id", ondelete="CASCADE"), nullable=False, index=True)

    event_type = Column(String(50), nullable=False)  # created/queued/sent/cancelled/marked_sent/etc
    message = Column(String(255), nullable=True)

    created_at = Column(DateTime(timezone=True), server_default=func.now(), nullable=False)

    broadcast = relationship("BroadcastMessage", back_populates="events")

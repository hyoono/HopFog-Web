from fastapi import APIRouter, Depends, Form, HTTPException
from sqlalchemy.orm import Session
from datetime import datetime
from database.models import FogDevice, FogMessage, User
from database.deps import get_db
from routes.auth import verify_token


fog_router = APIRouter(
    prefix="/api/fog-nodes",
    tags=["Fog Nodes"]
)

@fog_router.post("/send")
def send_to_fog_node(
    device_id: int = Form(...),
    message_type: str = Form(...),  # command, alert, data, notification
    content: str = Form(...),
    db: Session = Depends(get_db)
):
    # Find the fog device
    device = db.query(FogDevice).filter(FogDevice.id == device_id).first()
    
    if not device:
        raise HTTPException(status_code=404, detail="Fog device not found")
    
    if device.status != "active":
        raise HTTPException(status_code=400, detail="Fog device is not active")
    
    # Create message record
    new_message = FogMessage(
        device_id=device_id,
        direction="outgoing",
        message_type=message_type,
        content=content,
        status="sent",
        created_at=datetime.utcnow()
    )
    
    db.add(new_message)
    db.commit()
    db.refresh(new_message)
    
    return {
        "success": True,
        "message": "Message sent to fog node",
        "data": {
            "message_id": new_message.id,
            "device_id": device_id,
            "device_name": device.device_name,
            "device_ip": device.ip_address,
            "message_type": message_type,
            "content": content,
            "status": "sent",
            "timestamp": new_message.created_at.isoformat()
        }
    }


@fog_router.post("/broadcast")
def broadcast_to_fog_nodes(
    message_type: str = Form(...),
    content: str = Form(...),
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token)
):
    # Only admins can broadcast
    if current_user.role != "admin":
        raise HTTPException(status_code=403, detail="Only admins can broadcast to all fog nodes")
    
    # Get all active fog devices
    devices = db.query(FogDevice).filter(FogDevice.status == "active").all()
    
    if not devices:
        raise HTTPException(status_code=404, detail="No active fog devices found")
    
    sent_messages = []
    
    for device in devices:
        new_message = FogMessage(
            device_id=device.id,
            direction="outgoing",
            message_type=message_type,
            content=content,
            status="sent",
            created_at=datetime.utcnow()
        )
        db.add(new_message)
        sent_messages.append({
            "device_id": device.id,
            "device_name": device.device_name
        })
    
    db.commit()
    
    return {
        "success": True,
        "message": f"Message broadcast to {len(devices)} fog nodes",
        "devices": sent_messages
    }

#----- Receive from fog node -----

@fog_router.post("/receive")
def receive_from_fog_node(
    device_id: int = Form(...),
    message_type: str = Form(...),  # status, sensor_data, alert, log
    content: str = Form(...),
    db: Session = Depends(get_db)
):
    # Find the fog device
    device = db.query(FogDevice).filter(FogDevice.id == device_id).first()
    
    if not device:
        raise HTTPException(status_code=404, detail="Fog device not found")
    
    # Create incoming message record
    new_message = FogMessage(
        device_id=device_id,
        direction="incoming",
        message_type=message_type,
        content=content,
        status="received",
        created_at=datetime.utcnow()
    )
    
    db.add(new_message)
    db.commit()
    db.refresh(new_message)
    
    return {
        "success": True,
        "message": "Data received from fog node",
        "data": {
            "message_id": new_message.id,
            "device_id": device_id,
            "message_type": message_type,
            "status": "received",
            "timestamp": new_message.created_at.isoformat()
        }
    }

@fog_router.post("{device_id}/status")
def fog_node_status_update(
    device_id: int,
    status: str = Form(...),  # online, offline, busy, error
    cpu_usage: float = Form(None),
    memory_usage: float = Form(None),
    temperature: float = Form(None),
    db: Session = Depends(get_db)
):
    device = db.query(FogDevice).filter(FogDevice.id == device_id).first()
    
    if not device:
        raise HTTPException(status_code=404, detail="Fog device not found")
    
    # Update device status
    device.status = "active" if status == "online" else "inactive"
    
    # Log the status update
    status_log = FogMessage(
        device_id=device_id,
        direction="incoming",
        message_type="status",
        content=f"Status: {status}, CPU: {cpu_usage}%, Memory: {memory_usage}%, Temp: {temperature}°C",
        status="received",
        created_at=datetime.utcnow()
    )
    
    db.add(status_log)
    db.commit()
    
    return {
        "success": True,
        "message": "Status updated",
        "device_id": device_id,
        "status": status
    }

#----Get messages-----

@fog_router.get("/{device_id}/messages")
def get_fog_node_messages(
    device_id: int,
    direction: str = None,  # incoming, outgoing, or None for all
    limit: int = 50,
    db: Session = Depends(get_db)
):
    device = db.query(FogDevice).filter(FogDevice.id == device_id).first()
    
    if not device:
        raise HTTPException(status_code=404, detail="Fog device not found")
    
    query = db.query(FogMessage).filter(FogMessage.device_id == device_id)
    
    if direction:
        query = query.filter(FogMessage.direction == direction)
    
    messages = query.order_by(FogMessage.created_at.desc()).limit(limit).all()
    
    return {
        "device_id": device_id,
        "device_name": device.device_name,
        "count": len(messages),
        "messages": [
            {
                "id": msg.id,
                "direction": msg.direction,
                "message_type": msg.message_type,
                "content": msg.content,
                "status": msg.status,
                "timestamp": msg.created_at.isoformat() if msg.created_at else None
            }
            for msg in messages
        ]
    }


@fog_router.get("/{device_id}/pending")
def get_pending_messages(
    device_id: int,
    db: Session = Depends(get_db)
):
    device = db.query(FogDevice).filter(FogDevice.id == device_id).first()
    
    if not device:
        raise HTTPException(status_code=404, detail="Fog device not found")
    
    # Get outgoing messages that haven't been delivered
    pending = db.query(FogMessage).filter(
        FogMessage.device_id == device_id,
        FogMessage.direction == "outgoing",
        FogMessage.status == "sent"
    ).order_by(FogMessage.created_at.asc()).all()
    
    return {
        "device_id": device_id,
        "count": len(pending),
        "messages": [
            {
                "id": msg.id,
                "message_type": msg.message_type,
                "content": msg.content,
                "timestamp": msg.created_at.isoformat() if msg.created_at else None
            }
            for msg in pending
        ]
    }

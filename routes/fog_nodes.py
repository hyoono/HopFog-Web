from fastapi import APIRouter, Depends, Form, HTTPException
from sqlalchemy.orm import Session
from datetime import datetime
from database.models import FogDevice, FogMessage, User
from database.deps import get_db
from routes.auth import verify_token


fog_router = APIRouter(
    prefix="/api/fog-devices",
    tags=["Fog Device API"]
)


@fog_router.post("/register")
def register_fog_device(
    device_name: str = Form(...),
    storage_total: str = Form(None),
    storage_used: str = Form(None),
    db: Session = Depends(get_db)
):
    """
    Fog device calls this on startup to register or reconnect.
    """
    existing = db.query(FogDevice).filter(FogDevice.device_name == device_name).first()
    
    if existing:
        existing.status = "active"
        if storage_total:
            existing.storage_total = storage_total
        if storage_used:
            existing.storage_used = storage_used
        db.commit()
        return {
            "success": True,
            "message": "Device reconnected",
            "device_id": existing.id,
            "device_name": existing.device_name
        }
    new_device = FogDevice(
    device_name=device_name,
    status="active",
    storage_total=storage_total,
    storage_used=storage_used,
    )
    db.add(new_device)
    db.commit()
    db.refresh(new_device)
    
    return {
        "success": True,
        "message": "Device registered",
        "device_id": new_device.id,
        "device_name": new_device.device_name
    }


@fog_router.post("/{device_id}/disconnect")
def fog_device_disconnect(
    device_id: int,
    db: Session = Depends(get_db)
):
    """
    Fog device calls this when shutting down.
    """
    device = db.query(FogDevice).filter(FogDevice.id == device_id).first()
    
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")
    
    device.status = "inactive"
    device.connected_users = 0
    db.commit()
    
    return {
        "success": True,
        "message": "Device disconnected",
        "device_id": device_id
    }

@fog_router.get("/")
def get_all_fog_devices(
    db: Session = Depends(get_db)
):
    """
    Returns all fog devices with their current stats.
    """
    devices = db.query(FogDevice).all()
    
    return {
        "total": len(devices),
        "active": len([d for d in devices if d.status == "active"]),
        "inactive": len([d for d in devices if d.status != "active"]),
        "devices": [
            {
                "id": d.id,
                "device_name": d.device_name,
                "status": d.status,
                "storage_total": d.storage_total,
                "storage_used": d.storage_used,
                "connected_users": d.connected_users,
            }
            for d in devices
        ]
    }


@fog_router.post("{device_id}/status")
def fog_node_status_update(
    device_id: int,
    status: str = Form(...),  # online, offline, busy, error
    memory_usage: float = Form(None),
    db: Session = Depends(get_db)
):
    device = db.query(FogDevice).filter(FogDevice.id == device_id).first()
    
    if not device:
        raise HTTPException(status_code=404, detail="Fog device not found")
    
    device.status = "active" if status == "online" else "inactive"
    device.memory_usage = memory_usage
    
    status_log = FogMessage(
        device_id=device_id,
        direction="incoming",
        message_type="status",
        content=f"Status: {status}, Memory: {memory_usage}%", 
        status="received",
    )
    
    db.add(status_log)
    db.commit()
    
    return {
        "success": True,
        "message": "Status updated",
        "device_id": device_id,
        "status": status
    }

@fog_router.get("/specific/{device_id}")
def get_fog_device(
    device_id: int,
    db: Session = Depends(get_db)
):
    """
    Returns details for a specific fog device.
    """
    device = db.query(FogDevice).filter(FogDevice.id == device_id).first()
    
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")
    
    message_count = db.query(FogMessage).filter(FogMessage.device_id == device_id).count()
    
    return {
        "id": device.id,
        "device_name": device.device_name,
        "status": device.status,
        "storage_total": device.storage_total,
        "storage_used": device.storage_used,
        "connected_users": device.connected_users,
        "total_messages": message_count,
    }
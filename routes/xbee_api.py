from fastapi import APIRouter, HTTPException
from pydantic import BaseModel
from services.xbee_service import XBeeService

router = APIRouter(prefix="/api/xbee", tags=["xbee"])
svc = XBeeService()

class BroadcastReq(BaseModel):
    text: str

@router.get("/info")
def info():
    try:
        return svc.info()
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/test-broadcast")
def test_broadcast(req: BroadcastReq):
    try:
        return svc.send_broadcast(req.text)
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/broadcast")
def broadcast(req: BroadcastReq):
    try:
        return svc.send_broadcast(req.text)
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.get("/received")
def received():
    try:
        return {"messages": svc.get_received()}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/received/clear")
def clear_received():
    try:
        return svc.clear_received()
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
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

@router.get("/discover")
def discover(timeout_s: int = 8):
    try:
        return {"devices": svc.discover(timeout_s=timeout_s)}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/test-broadcast")
def test_broadcast(req: BroadcastReq):
    try:
        return svc.send_broadcast(req.text)
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
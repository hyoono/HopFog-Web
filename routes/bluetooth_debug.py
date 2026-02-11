from fastapi import APIRouter, Request
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates

templates = Jinja2Templates(directory="templates")
router = APIRouter()

@router.get("/admin/bluetooth-debug", response_class=HTMLResponse)
def bluetooth_debug_page(request: Request):
    # page will fetch data via JS from /api/bluetooth/outbox/list
    return templates.TemplateResponse("bluetooth_debug.html", {"request": request})

import bcrypt
from fastapi import FastAPI, Request, Depends, Form, HTTPException
from fastapi.responses import HTMLResponse, RedirectResponse
from fastapi.templating import Jinja2Templates
from fastapi.staticfiles import StaticFiles

from sqlalchemy.orm import Session
from sqlalchemy.orm import aliased
from sqlalchemy import func as sql_func

from database.connection import engine
from database.models import Base, Message, MessageRecipient, User, FogDevice, FogMessage, BroadcastMessage
from database.deps import get_db

from routes.auth import verify_password, get_password_hash, create_access_token, verify_token

from routes.users import router as users_router
from routes.messages import router as messages_router
from routes.resident_admin import router as resident_admin_router
from routes.admin_messaging import router as admin_messaging_router
from routes.fog_nodes import fog_router
from routes.xbee_api import router as xbee_router
from routes.node_api import router as node_router

from services.broadcast_dispatcher import dispatcher

from services.broadcast_dispatcher import dispatcher

from dotenv import load_dotenv
load_dotenv(override=True)


app = FastAPI()

app.mount("/static", StaticFiles(directory="static"), name="static")
templates = Jinja2Templates(directory="templates")

app.include_router(users_router)
app.include_router(messages_router)
app.include_router(resident_admin_router)
app.include_router(admin_messaging_router)
app.include_router(fog_router)
app.include_router(xbee_router)
app.include_router(node_router)


@app.on_event("startup")
def on_startup():
    Base.metadata.create_all(bind=engine)


@app.on_event("startup")
async def on_startup():
    await dispatcher.start()
    # Start XBee node protocol handler
    try:
        from services.xbee_service import xbee_service
        from services.node_protocol import handle_node_command
        xbee_service.open()
        xbee_service.set_command_handler(handle_node_command)
        print("[XBEE] Node protocol handler registered")
    except Exception as e:
        print(f"[XBEE] Could not start: {e}")

@app.on_event("shutdown")
async def on_shutdown():
    await dispatcher.stop()

@app.get("/", response_class=HTMLResponse)
def login_page(request: Request):
    return templates.TemplateResponse("login.html", {"request": request})

@app.post("/login")
async def login(
    request: Request,
    email: str = Form(...),
    password: str = Form(...),
    db: Session = Depends(get_db)
):
    print(f"\n=== LOGIN ATTEMPT ===")
    print(f"Email: {email}")
    print(f"Password entered: '{password}'")
    print(f"Password length: {len(password)} chars, {len(password.encode('utf-8'))} bytes")
    
    user = db.query(User).filter(User.email == email).first()
    
    if not user:
        print(f"User not found!")
        return templates.TemplateResponse("login.html", {
            "request": request,
            "error": "Invalid email or password"
        })    
    print(f"User found: {user.username}")
    print(f"Stored hash: {user.password_hash}")
    print(f"Hash type: {type(user.password_hash)}")

    if user.role != "admin":
        print(f"User is not an admin, role: {user.role}")
        return templates.TemplateResponse("login.html", {
            "request": request,
            "error": "This account is not authorized for web access. Please use the mobile app."
        })
    
    # Manual verification test
    try:
        password_bytes = password.encode('utf-8')
        hash_bytes = user.password_hash.encode('utf-8')
        
        print(f"Password bytes: {password_bytes}")
        print(f"Hash bytes length: {len(hash_bytes)}")
        
        result = bcrypt.checkpw(password_bytes, hash_bytes)
        print(f"bcrypt.checkpw result: {result}")
        
        if not result:
            print(f"Password verification FAILED")
            return templates.TemplateResponse("login.html", {
            "request": request,
            "error": "Invalid email or password"
        })           
    except Exception as e:
        print(f"Exception during verification: {type(e).__name__}: {e}")
        import traceback
        traceback.print_exc()
        return templates.TemplateResponse("login.html", {
            "request": request,
            "error": "Invalid email or password"
        })
        
    print(f"Login successful!")
    
    token = create_access_token(data={"sub": user.email})
    response = RedirectResponse(url="/dashboard", status_code=302)
    response.set_cookie(key="access_token", value=f"Bearer {token}", httponly=True)
    return response





@app.get("/register", response_class=HTMLResponse)
def register(request: Request):
    return templates.TemplateResponse("register.html", {"request": request})

@app.post("/register")
def register(
    request: Request,
    username: str = Form(...),
    email: str = Form(...),
    password: str = Form(...),
    db: Session = Depends(get_db)
):
    # Check if email exists
    if db.query(User).filter(User.email == email).first():
        return templates.TemplateResponse("register.html", {
            "request": request,
            "error": "Email already registered"
        })
    
    # Check if username exists
    if db.query(User).filter(User.username == username).first():
        return templates.TemplateResponse("register.html", {
            "request": request,
            "error": "Username already taken"
        })
    
    # Create user with admin role (web registration = admin)
    new_user = User(
        username=username,
        email=email,
        password_hash=get_password_hash(password),
        role="admin",  # Web users are admins
        is_active=True
    )
    
    db.add(new_user)
    db.commit()
    
    return RedirectResponse(url="/?registered=true", status_code=302)




@app.get("/dashboard", response_class=HTMLResponse)
def dashboard(request: Request, db: Session = Depends(get_db), current_user: User= Depends(verify_token)):
    Sender = aliased(User)

        # Fog device stats from database
    fog_nodes_count = db.query(FogDevice).count()
    active_fog_nodes = db.query(FogDevice).filter(FogDevice.status == "active").count()
    inactive_fog_nodes = fog_nodes_count - active_fog_nodes
    
    # Total connected users across all fog devices
    people_connected = db.query(sql_func.coalesce(sql_func.sum(FogDevice.connected_users), 0)).scalar()
    
    # Storage info - get total storage from all devices
    devices = db.query(FogDevice).all()
    storage_used = 0
    storage_total = 0
    for device in devices:
        try:
            if device.storage_used:
                storage_used += float(device.storage_used.replace("GB", "").replace("MB", "").strip())
            if device.storage_total:
                storage_total += float(device.storage_total.replace("GB", "").replace("MB", "").strip())
        except (ValueError, AttributeError):
            pass
    
    storage_display = f"{storage_used:.1f}GB / {storage_total:.1f}GB" if storage_total > 0 else "N/A"
    
    total_sos_alerts = db.query(BroadcastMessage).filter(
        BroadcastMessage.msg_type.in_(["alert", "sos"])
    ).count()

    # Query messages with sender and recipients
    messages_query = db.query(
        Message.id,
        Message.body,
        Message.created_at,
        Sender.username.label('sender_username')
    ).join(Sender, Message.sender_id == Sender.id).all()
    
    messages = []
    for msg in messages_query:
        recipients = db.query(User.username).join(
            MessageRecipient, User.id == MessageRecipient.user_id
        ).filter(MessageRecipient.message_id == msg.id).all()
        
        messages.append({
            'id': msg.id,
            'from': msg.sender_username,
            'to': [r.username for r in recipients],
            'message': msg.body,
            'date': msg.created_at
        })

    return templates.TemplateResponse("dashboard.html", {
        "request": request,
        "current_user": current_user,
        "messages": messages,
        "fog_nodes_count": fog_nodes_count,
        "active_fog_nodes": active_fog_nodes,
        "inactive_fog_nodes": inactive_fog_nodes,
        "people_connected": people_connected,
        "storage_display": storage_display,
        "total_sos_alerts": total_sos_alerts,
    })



    



@app.get("/users", response_class=HTMLResponse)
def users(request: Request, db: Session = Depends(get_db), current_user: User = Depends(verify_token)):
    users_list = db.query(User).all()

    return templates.TemplateResponse("user.html", {
        "request": request,
        "users": users_list,
        "current_user": current_user
    })

@app.get("/logs", response_class=HTMLResponse)
def logs(request: Request, db: Session = Depends(get_db), current_user: User = Depends(verify_token)):
    Sender = aliased(User)
    
    messages_query = db.query(
        Message.id,
        Message.body,
        Message.created_at,
        Sender.username.label('sender_username')
    ).join(Sender, Message.sender_id == Sender.id).all()
    
    messages = []
    for msg in messages_query:
        recipients = db.query(User.username).join(
            MessageRecipient, User.id == MessageRecipient.user_id
        ).filter(MessageRecipient.message_id == msg.id).all()
        
        messages.append({
            'id': msg.id,
            'from': msg.sender_username,
            'to': [r.username for r in recipients],
            'message': msg.body,
            'date': msg.created_at
        })
    
    return templates.TemplateResponse("logs.html", {
        "request": request,
        "messages": messages,
        "current_user": current_user
    })


@app.delete("/api/messages/{message_id}")
def delete_message(message_id: int, db: Session = Depends(get_db)):
    db.query(MessageRecipient).filter(MessageRecipient.message_id == message_id).delete()
    
    # Delete the message
    message = db.query(Message).filter(Message.id == message_id).first()
    if message:
        db.delete(message)
        db.commit()
        return {"message": "Message deleted successfully"}
    return {"error": "Message not found"}

# @app.get("/debug/users")
# def debug_users(db: Session = Depends(get_db)):
#     users = db.query(User).all()
#     return [{
#         "id": user.id,
#         "email": user.email,
#         "username": user.username,
#         "created_at": user.created_at
#     } for user in users]


@app.get("/fog_nodes", response_class=HTMLResponse)
def fog_nodes(request: Request, db: Session = Depends(get_db), current_user: User = Depends(verify_token)):
    devices = db.query(FogDevice).all()
    
    fog_nodes_data = []
    total_connected_users = 0
    
    for device in devices:
        message_count = db.query(FogMessage).filter(
            FogMessage.device_id == device.id
        ).count()
        
        total_connected_users += device.connected_users or 0
        
        fog_nodes_data.append({
            'id': device.id,
            'name': device.device_name,
            'ip_address': device.ip_address or 'N/A',
            'status': device.status,
            'storage_total': device.storage_total or 'N/A',
            'storage_used': device.storage_used or 'N/A',
            'connected_users': device.connected_users or 0,
            'message_count': message_count,
        })
    
    total_devices = len(fog_nodes_data)
    active_devices = len([d for d in fog_nodes_data if d['status'] == 'active'])
    inactive_devices = total_devices - active_devices


    return templates.TemplateResponse("fog_nodes.html", {
        "request": request,
        "current_user": current_user,
        "fog_nodes": fog_nodes_data,
        "total_devices": total_devices,
        "active_devices": active_devices,
        "inactive_devices": inactive_devices,
        "total_connected_users": total_connected_users,
    })



@app.get("/settings", response_class=HTMLResponse)
def settings(request: Request, db: Session = Depends(get_db), current_user: User = Depends(verify_token)):
    return templates.TemplateResponse("settings.html", {
        "request": request,
        "current_user": current_user
    })

@app.post("/settings/change-password", response_class=HTMLResponse)
async def change_password(
    request: Request,
    current_password: str = Form(...),
    new_password: str = Form(...),
    confirm_password: str = Form(...),
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token)
):
    error = None
    success = None
    
    # Check if new passwords match
    if new_password != confirm_password:
        error = "New passwords do not match"
    
    # Verify current password
    elif not verify_password(current_password, current_user.password_hash):
        error = "Current password is incorrect"
    
    # Check minimum length
    elif len(new_password) < 6:
        error = "New password must be at least 6 characters"
    
    else:
        # Update password
        current_user.password_hash = get_password_hash(new_password)
        db.commit()
        success = "Password changed successfully!"
    
    return templates.TemplateResponse("settings.html", {
        "request": request,
        "current_user": current_user,
        "success": success,
        "error": error
    })



# Mobile Login API
@app.post("/api/mobile/login")
def mobile_login(
    email: str = Form(...),
    password: str = Form(...),
    db: Session = Depends(get_db)
):
    # Find user
    user = db.query(User).filter(User.email == email).first()
    
    if not user:
        raise HTTPException(status_code=401, detail="Invalid email or password")
    
    # Check if user is a mobile user
    if user.role != "mobile":
        raise HTTPException(status_code=403, detail="This account is not authorized for mobile access")
    
    # Verify password - FIXED: use password_hash
    if not verify_password(password, user.password_hash):
        raise HTTPException(status_code=401, detail="Invalid email or password")
    
    # Check if active
    if not user.is_active:
        raise HTTPException(status_code=403, detail="Account is inactive")
    
    # Create token
    access_token = create_access_token(data={"sub": user.email, "role": "mobile"})
    
    return {
        "access_token": access_token,
        "token_type": "bearer",
        "user": {
            "id": user.id,
            "username": user.username,
            "email": user.email,
            "role": user.role
        }
    }

# Web Admin - Create Mobile User
@app.post("/api/admin/create-mobile-user")
def create_mobile_user(
    username: str = Form(...),
    email: str = Form(...),
    password: str = Form(...),
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token)
):
    # Only admins can create mobile users
    if current_user.role != "admin":
        raise HTTPException(status_code=403, detail="Only admins can create mobile users")
    
    # Check if email exists
    if db.query(User).filter(User.email == email).first():
        raise HTTPException(status_code=400, detail="Email already registered")
    
    # Check if username exists
    if db.query(User).filter(User.username == username).first():
        raise HTTPException(status_code=400, detail="Username already taken")
    
    # Create user with mobile role
    new_user = User(
        username=username,
        email=email,
        password_hash=get_password_hash(password),
        role="mobile",
        is_active=True
    )
    
    db.add(new_user)
    db.commit()
    db.refresh(new_user)
    
    return {
        "message": "Mobile user created successfully",
        "user": {
            "id": new_user.id,
            "username": new_user.username,
            "email": new_user.email,
            "role": new_user.role
        }
    }

# Toggle User Status (Activate/Deactivate)
@app.put("/api/users/{user_id}/toggle-status")
def toggle_user_status(
    user_id: int,
    db: Session = Depends(get_db),
    current_user: User = Depends(verify_token)
):
    # Only admins can toggle user status
    if current_user.role != "admin":
        raise HTTPException(status_code=403, detail="Only admins can modify user status")
    
    # Find the user
    user = db.query(User).filter(User.id == user_id).first()
    
    if not user:
        raise HTTPException(status_code=404, detail="User not found")
    
    # Prevent modifying admin users
    if user.role == "admin":
        raise HTTPException(status_code=400, detail="Cannot modify admin user status")
    
    # Prevent deactivating yourself
    if user.id == current_user.id:
        raise HTTPException(status_code=400, detail="Cannot modify your own status")
    
    # Toggle the status
    user.is_active = not user.is_active
    db.commit()
    
    status = "activated" if user.is_active else "deactivated"
    
    return {
        "message": f"User {status} successfully",
        "user": {
            "id": user.id,
            "username": user.username,
            "is_active": user.is_active
        }
    }



# Forgot Password - Simple Reset
@app.post("/forgot-password")
def forgot_password(
    email: str = Form(...),
    new_password: str = Form(...),
    db: Session = Depends(get_db)
):
    # Find user by email
    user = db.query(User).filter(User.email == email).first()
    
    if not user:
        return {"success": False, "message": "No account found with this email address."}
    
    # Only allow admins to reset password via web
    if user.role != "admin":
        return {"success": False, "message": "Mobile users must contact an administrator to reset their password."}
    
    # Check password length
    if len(new_password) < 6:
        return {"success": False, "message": "Password must be at least 6 characters."}
    
    # Update password
    user.password_hash = get_password_hash(new_password)
    db.commit()
    
    return {"success": True, "message": "Password reset successfully! You can now log in."}

# HopFog-Web Deployment Guide

This comprehensive guide covers deploying both versions of HopFog-Web in various environments.

## Table of Contents

- [Overview](#overview)
- [FastAPI Version Deployment](#fastapi-version-deployment)
  - [Prerequisites](#prerequisites)
  - [Local Development](#local-development)
  - [Production Server (Linux)](#production-server-linux)
  - [Docker Deployment](#docker-deployment)
  - [Cloud Platform Deployment](#cloud-platform-deployment)
  - [Reverse Proxy Setup](#reverse-proxy-setup)
- [ESP32-CAM Version Deployment](#esp32-cam-version-deployment)
- [Security Best Practices](#security-best-practices)
- [Monitoring and Maintenance](#monitoring-and-maintenance)
- [Troubleshooting](#troubleshooting)

---

## Overview

HopFog-Web offers two deployment options:

1. **FastAPI Version**: Full-featured Python web application for production servers
2. **ESP32-CAM Version**: Embedded microcontroller solution for edge deployment

Choose the deployment method that best fits your infrastructure and requirements.

---

## FastAPI Version Deployment

### Prerequisites

**System Requirements:**
- Linux, macOS, or Windows Server
- Python 3.7 or higher
- 512MB RAM minimum (2GB recommended)
- 1GB disk space
- Network connectivity

**Software Dependencies:**
- Python 3.7+
- pip (Python package manager)
- Virtual environment (recommended)
- Database (SQLite or PostgreSQL)

### Local Development

Perfect for development and testing.

#### 1. Clone the Repository

```bash
git clone https://github.com/hyoono/HopFog-Web.git
cd HopFog-Web
```

#### 2. Create Virtual Environment

```bash
# Create virtual environment
python3 -m venv env

# Activate virtual environment
source env/bin/activate  # Linux/macOS
# OR
env\Scripts\activate  # Windows
```

#### 3. Install Dependencies

```bash
pip install --upgrade pip
pip install fastapi uvicorn[standard] sqlalchemy python-dotenv bcrypt python-jose passlib
```

#### 4. Configure Environment

Create a `.env` file in the project root:

```bash
# .env file
DATABASE_URL=sqlite:///./capstone.db
SECRET_KEY=your-secret-key-here-change-in-production
ALGORITHM=HS256
ACCESS_TOKEN_EXPIRE_MINUTES=30
```

Generate a secure secret key:

```bash
python -c "import secrets; print(secrets.token_urlsafe(32))"
```

#### 5. Initialize Database

```bash
# Database will be automatically created on first run
# Tables are created via SQLAlchemy in app/main.py on_startup event
```

#### 6. Run Development Server

```bash
uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

Access the application at: `http://localhost:8000`

**Default Credentials:**
- Check the application's user registration page or create an admin user through the web interface

---

### Production Server (Linux)

For production deployment on Ubuntu/Debian servers.

#### 1. Server Preparation

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Install Python and dependencies
sudo apt install -y python3 python3-pip python3-venv nginx

# Create application user
sudo useradd -m -s /bin/bash hopfog
sudo usermod -aG sudo hopfog
```

#### 2. Deploy Application

```bash
# Switch to application user
sudo su - hopfog

# Clone repository
cd /home/hopfog
git clone https://github.com/hyoono/HopFog-Web.git
cd HopFog-Web

# Create virtual environment
python3 -m venv env
source env/bin/activate

# Install dependencies
pip install --upgrade pip
pip install fastapi uvicorn[standard] sqlalchemy python-dotenv bcrypt python-jose passlib gunicorn
```

#### 3. Configure Production Environment

```bash
# Create production .env file
nano .env
```

**Production .env example:**

```env
# Database Configuration
DATABASE_URL=postgresql://hopfog_user:secure_password@localhost/hopfog_db

# Security
SECRET_KEY=CHANGE_THIS_TO_A_SECURE_RANDOM_STRING
ALGORITHM=HS256
ACCESS_TOKEN_EXPIRE_MINUTES=30

# Application
DEBUG=False
ALLOWED_HOSTS=yourdomain.com,www.yourdomain.com
```

#### 4. Setup PostgreSQL (Optional but Recommended)

```bash
# Install PostgreSQL
sudo apt install -y postgresql postgresql-contrib

# Create database and user
sudo -u postgres psql << EOF
CREATE DATABASE hopfog_db;
CREATE USER hopfog_user WITH PASSWORD 'secure_password';
GRANT ALL PRIVILEGES ON DATABASE hopfog_db TO hopfog_user;
\q
EOF
```

#### 5. Create Systemd Service

Create `/etc/systemd/system/hopfog.service`:

```bash
sudo nano /etc/systemd/system/hopfog.service
```

**Service file content:**

```ini
[Unit]
Description=HopFog Web Application
After=network.target

[Service]
Type=notify
User=hopfog
Group=hopfog
WorkingDirectory=/home/hopfog/HopFog-Web
Environment="PATH=/home/hopfog/HopFog-Web/env/bin"
ExecStart=/home/hopfog/HopFog-Web/env/bin/gunicorn app.main:app \
    --workers 4 \
    --worker-class uvicorn.workers.UvicornWorker \
    --bind 0.0.0.0:8000 \
    --access-logfile /home/hopfog/HopFog-Web/logs/access.log \
    --error-logfile /home/hopfog/HopFog-Web/logs/error.log
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

#### 6. Create Log Directory

```bash
sudo mkdir -p /home/hopfog/HopFog-Web/logs
sudo chown -R hopfog:hopfog /home/hopfog/HopFog-Web/logs
```

#### 7. Enable and Start Service

```bash
# Reload systemd
sudo systemctl daemon-reload

# Enable service to start on boot
sudo systemctl enable hopfog

# Start service
sudo systemctl start hopfog

# Check status
sudo systemctl status hopfog

# View logs
sudo journalctl -u hopfog -f
```

#### 8. Service Management Commands

```bash
# Start service
sudo systemctl start hopfog

# Stop service
sudo systemctl stop hopfog

# Restart service
sudo systemctl restart hopfog

# Check status
sudo systemctl status hopfog

# View logs
sudo journalctl -u hopfog -n 100

# Follow logs in real-time
sudo journalctl -u hopfog -f
```

---

### Docker Deployment

Use Docker for containerized deployment.

#### 1. Create Dockerfile

Create `Dockerfile` in project root:

```dockerfile
FROM python:3.9-slim

# Set working directory
WORKDIR /app

# Install system dependencies
RUN apt-get update && apt-get install -y \
    gcc \
    && rm -rf /var/lib/apt/lists/*

# Copy requirements
COPY requirements.txt .

# Install Python dependencies
RUN pip install --no-cache-dir -r requirements.txt

# Copy application
COPY . .

# Create non-root user
RUN useradd -m -u 1000 hopfog && chown -R hopfog:hopfog /app
USER hopfog

# Expose port
EXPOSE 8000

# Run application
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
```

#### 2. Create docker-compose.yml

```yaml
version: '3.8'

services:
  web:
    build: .
    ports:
      - "8000:8000"
    environment:
      - DATABASE_URL=postgresql://hopfog:password@db:5432/hopfog
      - SECRET_KEY=${SECRET_KEY}
    volumes:
      - ./logs:/app/logs
    depends_on:
      - db
    restart: unless-stopped

  db:
    image: postgres:13-alpine
    environment:
      - POSTGRES_USER=hopfog
      - POSTGRES_PASSWORD=password
      - POSTGRES_DB=hopfog
    volumes:
      - postgres_data:/var/lib/postgresql/data
    restart: unless-stopped

volumes:
  postgres_data:
```

#### 3. Deploy with Docker Compose

```bash
# Build and start containers
docker-compose up -d

# View logs
docker-compose logs -f

# Stop containers
docker-compose down

# Rebuild and restart
docker-compose up -d --build
```

---

### Cloud Platform Deployment

#### AWS Deployment

**Option 1: EC2 Instance**

1. Launch EC2 instance (t2.micro or larger)
2. Follow [Production Server](#production-server-linux) instructions
3. Configure security groups to allow HTTP/HTTPS traffic
4. Associate Elastic IP for static IP address
5. Use Route 53 for DNS management

**Option 2: Elastic Beanstalk**

1. Install EB CLI: `pip install awsebcli`
2. Initialize: `eb init -p python-3.9 hopfog-web`
3. Create environment: `eb create hopfog-prod`
4. Deploy: `eb deploy`

**Option 3: ECS (Fargate)**

1. Push Docker image to ECR
2. Create ECS cluster
3. Define task definition
4. Create service with load balancer

#### Azure Deployment

**Azure App Service:**

```bash
# Install Azure CLI
curl -sL https://aka.ms/InstallAzureCLIDeb | sudo bash

# Login
az login

# Create resource group
az group create --name hopfog-rg --location eastus

# Create app service plan
az appservice plan create --name hopfog-plan --resource-group hopfog-rg --sku B1 --is-linux

# Create web app
az webapp create --resource-group hopfog-rg --plan hopfog-plan --name hopfog-web --runtime "PYTHON|3.9"

# Deploy
az webapp up --name hopfog-web --resource-group hopfog-rg
```

#### Google Cloud Platform

**App Engine:**

Create `app.yaml`:

```yaml
runtime: python39

entrypoint: gunicorn -w 4 -k uvicorn.workers.UvicornWorker app.main:app

env_variables:
  DATABASE_URL: "postgresql://user:pass@/dbname?host=/cloudsql/project:region:instance"
```

Deploy:

```bash
gcloud app deploy
```

---

### Reverse Proxy Setup

Use Nginx as a reverse proxy for production.

#### 1. Install Nginx

```bash
sudo apt install -y nginx
```

#### 2. Configure Nginx

Create `/etc/nginx/sites-available/hopfog`:

```nginx
# HTTP to HTTPS redirect
server {
    listen 80;
    listen [::]:80;
    server_name yourdomain.com www.yourdomain.com;
    
    # Redirect all HTTP requests to HTTPS
    return 301 https://$server_name$request_uri;
}

# HTTPS server
server {
    listen 443 ssl http2;
    listen [::]:443 ssl http2;
    server_name yourdomain.com www.yourdomain.com;

    # SSL configuration
    ssl_certificate /etc/letsencrypt/live/yourdomain.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/yourdomain.com/privkey.pem;
    ssl_session_timeout 1d;
    ssl_session_cache shared:SSL:50m;
    ssl_session_tickets off;

    # Modern SSL configuration
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384;
    ssl_prefer_server_ciphers off;

    # HSTS
    add_header Strict-Transport-Security "max-age=31536000; includeSubDomains" always;

    # Security headers
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;
    add_header X-XSS-Protection "1; mode=block" always;

    # Logging
    access_log /var/log/nginx/hopfog_access.log;
    error_log /var/log/nginx/hopfog_error.log;

    # Proxy settings
    location / {
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        
        # WebSocket support
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        
        # Timeouts
        proxy_connect_timeout 60s;
        proxy_send_timeout 60s;
        proxy_read_timeout 60s;
    }

    # Static files (optional - if serving directly)
    location /static {
        alias /home/hopfog/HopFog-Web/static;
        expires 30d;
        add_header Cache-Control "public, immutable";
    }
}
```

#### 3. Enable Site and Restart Nginx

```bash
# Enable site
sudo ln -s /etc/nginx/sites-available/hopfog /etc/nginx/sites-enabled/

# Test configuration
sudo nginx -t

# Restart Nginx
sudo systemctl restart nginx
```

#### 4. Setup SSL with Let's Encrypt

```bash
# Install Certbot
sudo apt install -y certbot python3-certbot-nginx

# Obtain certificate
sudo certbot --nginx -d yourdomain.com -d www.yourdomain.com

# Test auto-renewal
sudo certbot renew --dry-run
```

---

## ESP32-CAM Version Deployment

For detailed ESP32-CAM deployment, see [ESP32_README.md](ESP32_README.md) and [QUICKSTART.md](QUICKSTART.md).

### Quick Deployment Checklist

#### 1. Hardware Preparation

- [ ] ESP32-CAM module (AI-Thinker)
- [ ] MicroSD card (4-32GB, FAT32 formatted)
- [ ] FTDI programmer
- [ ] 5V power supply (500mA minimum)

#### 2. Software Setup

```bash
# Install Arduino IDE
# Add ESP32 board support
# Install ArduinoJson library
```

#### 3. Configuration

Edit `esp32_hopfog/esp32_hopfog.ino`:

```cpp
// WiFi Configuration
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Admin Credentials
const char* admin_username = "admin";
const char* admin_password = "CHANGE_THIS_PASSWORD";
```

#### 4. Upload and Deploy

```bash
# Connect ESP32-CAM via FTDI
# Select Board: "AI Thinker ESP32-CAM"
# Upload sketch
# Remove IO0-GND connection
# Press RESET
```

#### 5. Access Web Interface

```
http://<ESP32_IP_ADDRESS>
```

### Production Deployment (ESP32-CAM)

**Best Practices:**

1. **Change Default Credentials**: Update admin username and password
2. **Secure Network**: Use WPA2/WPA3 WiFi encryption
3. **Static IP**: Configure static IP for consistent access
4. **Power Supply**: Use stable 5V supply with adequate current
5. **SD Card Backups**: Regularly backup SD card data
6. **Physical Security**: Secure device in tamper-proof enclosure
7. **Network Isolation**: Place on separate VLAN if possible

**OTA Updates** (Future Enhancement):

Consider implementing OTA (Over-The-Air) updates for production deployments to update firmware without physical access.

---

## Security Best Practices

### General Security

1. **Strong Passwords**: Use complex passwords (min 12 characters, mixed case, numbers, symbols)
2. **Secret Keys**: Generate cryptographically secure secret keys
3. **HTTPS Only**: Always use HTTPS in production
4. **Regular Updates**: Keep all dependencies updated
5. **Principle of Least Privilege**: Grant minimum necessary permissions
6. **Input Validation**: Validate all user inputs
7. **Rate Limiting**: Implement rate limiting on API endpoints

### FastAPI Security

```python
# Example rate limiting (add to main.py)
from slowapi import Limiter, _rate_limit_exceeded_handler
from slowapi.util import get_remote_address
from slowapi.errors import RateLimitExceeded

limiter = Limiter(key_func=get_remote_address)
app.state.limiter = limiter
app.add_exception_handler(RateLimitExceeded, _rate_limit_exceeded_handler)

@app.post("/login")
@limiter.limit("5/minute")
async def login(request: Request):
    # Login logic
    pass
```

### Database Security

1. **Connection String**: Never commit database credentials to git
2. **Encryption**: Use encrypted connections (SSL/TLS)
3. **Backups**: Implement automated backups
4. **Access Control**: Restrict database access to application user only

### Firewall Configuration

```bash
# UFW (Ubuntu)
sudo ufw allow 22/tcp    # SSH
sudo ufw allow 80/tcp    # HTTP
sudo ufw allow 443/tcp   # HTTPS
sudo ufw enable
```

---

## Monitoring and Maintenance

### Application Monitoring

#### 1. Log Management

**Centralized Logging:**

```bash
# Install log aggregator
sudo apt install -y rsyslog

# Configure application logging
# Edit /etc/rsyslog.d/50-hopfog.conf
```

**Log Rotation:**

Create `/etc/logrotate.d/hopfog`:

```
/home/hopfog/HopFog-Web/logs/*.log {
    daily
    rotate 14
    compress
    delaycompress
    notifempty
    create 0640 hopfog hopfog
    sharedscripts
    postrotate
        systemctl reload hopfog > /dev/null 2>&1 || true
    endscript
}
```

#### 2. Health Checks

Add health check endpoint to `app/main.py`:

```python
@app.get("/health")
def health_check():
    return {
        "status": "healthy",
        "timestamp": datetime.now().isoformat()
    }
```

#### 3. Monitoring Tools

**Prometheus + Grafana:**

```bash
# Install Prometheus
# Configure metrics endpoint
# Setup Grafana dashboards
```

**Basic Monitoring Script:**

```bash
#!/bin/bash
# check_hopfog.sh

URL="http://localhost:8000/health"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" $URL)

if [ $RESPONSE -eq 200 ]; then
    echo "HopFog is healthy"
    exit 0
else
    echo "HopFog is unhealthy (HTTP $RESPONSE)"
    # Send alert
    exit 1
fi
```

### Database Maintenance

#### Backup Script

```bash
#!/bin/bash
# backup_database.sh

BACKUP_DIR="/backup/hopfog"
DATE=$(date +%Y%m%d_%H%M%S)
DB_NAME="hopfog_db"

# Create backup directory
mkdir -p $BACKUP_DIR

# PostgreSQL backup
pg_dump $DB_NAME > $BACKUP_DIR/hopfog_$DATE.sql
gzip $BACKUP_DIR/hopfog_$DATE.sql

# Remove backups older than 30 days
find $BACKUP_DIR -name "*.sql.gz" -mtime +30 -delete

echo "Backup completed: hopfog_$DATE.sql.gz"
```

**Automate with Cron:**

```bash
# Edit crontab
crontab -e

# Add backup job (daily at 2 AM)
0 2 * * * /home/hopfog/scripts/backup_database.sh >> /var/log/hopfog_backup.log 2>&1
```

### System Updates

```bash
# Update system packages
sudo apt update && sudo apt upgrade -y

# Update Python packages
source env/bin/activate
pip list --outdated
pip install --upgrade package_name

# Restart application
sudo systemctl restart hopfog
```

---

## Troubleshooting

### FastAPI Troubleshooting

#### Service Won't Start

```bash
# Check service status
sudo systemctl status hopfog

# View detailed logs
sudo journalctl -u hopfog -n 100

# Check if port is already in use
sudo netstat -tulpn | grep 8000

# Test application manually
source env/bin/activate
uvicorn app.main:app --host 0.0.0.0 --port 8000
```

#### Database Connection Errors

```bash
# Check PostgreSQL status
sudo systemctl status postgresql

# Test database connection
psql -U hopfog_user -d hopfog_db -h localhost

# Check connection string in .env
cat .env | grep DATABASE_URL
```

#### Permission Errors

```bash
# Fix ownership
sudo chown -R hopfog:hopfog /home/hopfog/HopFog-Web

# Fix permissions
chmod 755 /home/hopfog/HopFog-Web
chmod 644 /home/hopfog/HopFog-Web/.env
```

#### High CPU/Memory Usage

```bash
# Check process resources
ps aux | grep gunicorn

# Monitor in real-time
htop

# Adjust worker count in systemd service
# Reduce --workers parameter
```

### ESP32-CAM Troubleshooting

#### Can't Upload Sketch

- Ensure IO0 is connected to GND during upload
- Check serial port selection
- Verify USB cable supports data transfer
- Try lower upload speed (115200)

#### WiFi Connection Issues

- Verify SSID and password
- Ensure 2.4GHz network (ESP32 doesn't support 5GHz)
- Check signal strength
- Review serial output for error messages

#### SD Card Not Detected

- Ensure card is FAT32 formatted
- Try different SD card
- Check card is properly inserted
- Verify power supply is adequate

#### Web Interface Not Accessible

- Check ESP32 IP address from serial monitor
- Verify firewall settings
- Ensure on same network
- Try accessing from different device

### Common Issues

#### 502 Bad Gateway

- Application not running: `sudo systemctl start hopfog`
- Check firewall: `sudo ufw status`
- Verify proxy configuration in Nginx

#### 500 Internal Server Error

- Check application logs: `sudo journalctl -u hopfog -f`
- Verify database connection
- Check file permissions

#### Slow Performance

- Increase worker count
- Optimize database queries
- Enable caching
- Check server resources (CPU, RAM, disk)

---

## Additional Resources

- **Main README**: [README.md](README.md)
- **ESP32-CAM Setup**: [ESP32_README.md](ESP32_README.md)
- **Quick Start Guide**: [QUICKSTART.md](QUICKSTART.md)
- **Architecture**: [ARCHITECTURE.md](ARCHITECTURE.md)
- **Wiring Guide**: [WIRING_GUIDE.md](WIRING_GUIDE.md)

---

## Support

For issues and questions:

1. Check the troubleshooting section
2. Review application logs
3. Check system logs: `sudo journalctl -xe`
4. Verify configuration files
5. Consult documentation

---

## Deployment Checklist

### Pre-Deployment

- [ ] Code reviewed and tested
- [ ] Dependencies updated
- [ ] Security audit completed
- [ ] Backup strategy in place
- [ ] Monitoring configured
- [ ] Documentation updated

### Post-Deployment

- [ ] Verify application is running
- [ ] Test all critical features
- [ ] Check logs for errors
- [ ] Verify database connections
- [ ] Test authentication
- [ ] Monitor performance metrics
- [ ] Verify backups are working
- [ ] Update DNS records (if applicable)
- [ ] Document any deployment-specific configurations

---

**Version**: 1.0.0  
**Last Updated**: 2026-02-20  
**Maintainer**: HopFog Development Team

#!/bin/bash
# HopFog Web - Quick Deployment Script for Production
# This script automates the deployment process on a fresh Ubuntu/Debian server

set -e  # Exit on error

echo "================================================"
echo "HopFog Web - Production Deployment Script"
echo "================================================"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo "Please run as root (use sudo)"
    exit 1
fi

# Configuration
APP_USER="hopfog"
APP_DIR="/home/$APP_USER/HopFog-Web"
REPO_URL="https://github.com/hyoono/HopFog-Web.git"
DOMAIN=""

# Get domain name
read -p "Enter your domain name (or press Enter to skip): " DOMAIN

echo ""
echo "Step 1: Updating system..."
apt update && apt upgrade -y

echo ""
echo "Step 2: Installing dependencies..."
apt install -y python3 python3-pip python3-venv nginx postgresql postgresql-contrib certbot python3-certbot-nginx git curl

echo ""
echo "Step 3: Creating application user..."
if id "$APP_USER" &>/dev/null; then
    echo "User $APP_USER already exists"
else
    useradd -m -s /bin/bash $APP_USER
    echo "User $APP_USER created"
fi

echo ""
echo "Step 4: Setting up PostgreSQL database..."
sudo -u postgres psql << EOF
-- Create database and user if they don't exist
SELECT 'CREATE DATABASE hopfog_db' WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'hopfog_db')\gexec
DO \$\$
BEGIN
    IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = 'hopfog_user') THEN
        CREATE USER hopfog_user WITH PASSWORD 'CHANGE_THIS_PASSWORD';
    END IF;
END
\$\$;
GRANT ALL PRIVILEGES ON DATABASE hopfog_db TO hopfog_user;
EOF

echo ""
echo "Step 5: Cloning repository..."
if [ -d "$APP_DIR" ]; then
    echo "Directory already exists. Pulling latest changes..."
    cd $APP_DIR
    sudo -u $APP_USER git pull
else
    sudo -u $APP_USER git clone $REPO_URL $APP_DIR
fi

cd $APP_DIR

echo ""
echo "Step 6: Setting up Python virtual environment..."
sudo -u $APP_USER python3 -m venv env
sudo -u $APP_USER $APP_DIR/env/bin/pip install --upgrade pip
sudo -u $APP_USER $APP_DIR/env/bin/pip install -r requirements.txt

echo ""
echo "Step 7: Configuring environment..."
if [ ! -f "$APP_DIR/.env" ]; then
    sudo -u $APP_USER cp $APP_DIR/.env.example $APP_DIR/.env
    
    # Generate secret key
    SECRET_KEY=$(python3 -c "import secrets; print(secrets.token_urlsafe(32))")
    
    # Update .env file
    sudo -u $APP_USER sed -i "s|SECRET_KEY=.*|SECRET_KEY=$SECRET_KEY|" $APP_DIR/.env
    sudo -u $APP_USER sed -i "s|DATABASE_URL=.*|DATABASE_URL=postgresql://hopfog_user:CHANGE_THIS_PASSWORD@localhost/hopfog_db|" $APP_DIR/.env
    
    echo "⚠️  IMPORTANT: Edit $APP_DIR/.env and update the database password!"
else
    echo ".env file already exists"
fi

echo ""
echo "Step 8: Creating log directory..."
sudo -u $APP_USER mkdir -p $APP_DIR/logs

echo ""
echo "Step 9: Installing systemd service..."
cp $APP_DIR/hopfog.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable hopfog
systemctl start hopfog

echo ""
echo "Step 10: Configuring Nginx..."
if [ ! -z "$DOMAIN" ]; then
    cp $APP_DIR/nginx.conf.example /etc/nginx/sites-available/hopfog
    sed -i "s|yourdomain.com|$DOMAIN|g" /etc/nginx/sites-available/hopfog
    ln -sf /etc/nginx/sites-available/hopfog /etc/nginx/sites-enabled/
    rm -f /etc/nginx/sites-enabled/default
    
    # Test nginx configuration
    nginx -t
    systemctl restart nginx
    
    echo ""
    echo "Step 11: Setting up SSL certificate..."
    certbot --nginx -d $DOMAIN -d www.$DOMAIN --non-interactive --agree-tos --register-unsafely-without-email || echo "SSL setup failed. Run certbot manually later."
else
    echo "Skipping Nginx and SSL setup (no domain provided)"
fi

echo ""
echo "Step 12: Configuring firewall..."
ufw allow 22/tcp
ufw allow 80/tcp
ufw allow 443/tcp
ufw --force enable

echo ""
echo "================================================"
echo "Deployment Complete!"
echo "================================================"
echo ""
echo "Next steps:"
echo "1. Edit $APP_DIR/.env and update database password"
echo "2. Restart service: sudo systemctl restart hopfog"
echo "3. Check status: sudo systemctl status hopfog"
echo "4. View logs: sudo journalctl -u hopfog -f"
echo ""
if [ ! -z "$DOMAIN" ]; then
    echo "Your application should be accessible at: https://$DOMAIN"
else
    echo "Your application is running on: http://$(hostname -I | awk '{print $1}'):8000"
fi
echo ""
echo "⚠️  IMPORTANT SECURITY TASKS:"
echo "1. Change database password in PostgreSQL"
echo "2. Update SECRET_KEY in .env file"
echo "3. Review and update security settings"
echo "================================================"

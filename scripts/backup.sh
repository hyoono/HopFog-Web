#!/bin/bash
# HopFog Database Backup Script
# Place in /home/hopfog/scripts/backup.sh
# Add to crontab: 0 2 * * * /home/hopfog/scripts/backup.sh

set -e

# Configuration
BACKUP_DIR="/home/hopfog/backups"
DB_NAME="hopfog_db"
DB_USER="hopfog_user"
DATE=$(date +%Y%m%d_%H%M%S)
RETENTION_DAYS=30

# Create backup directory if it doesn't exist
mkdir -p $BACKUP_DIR

echo "Starting backup at $(date)"

# PostgreSQL backup
echo "Backing up PostgreSQL database..."
PGPASSWORD="${DB_PASSWORD:-}" pg_dump -U $DB_USER -h localhost $DB_NAME > $BACKUP_DIR/hopfog_$DATE.sql

# Compress backup
echo "Compressing backup..."
gzip $BACKUP_DIR/hopfog_$DATE.sql

# Calculate backup size
BACKUP_SIZE=$(du -h $BACKUP_DIR/hopfog_$DATE.sql.gz | cut -f1)
echo "Backup completed: hopfog_$DATE.sql.gz ($BACKUP_SIZE)"

# Remove old backups
echo "Removing backups older than $RETENTION_DAYS days..."
find $BACKUP_DIR -name "hopfog_*.sql.gz" -mtime +$RETENTION_DAYS -delete

# List current backups
echo ""
echo "Current backups:"
ls -lh $BACKUP_DIR/hopfog_*.sql.gz

echo "Backup completed successfully at $(date)"

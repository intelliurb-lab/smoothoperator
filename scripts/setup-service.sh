#!/bin/bash
# scripts/setup-service.sh
# Sets up the smoothoperator systemd service based on the installation prefix.

set -e

PREFIX=${1:-/usr/local}
BIN_DIR="$PREFIX/bin"
CONFIG_DIR="/etc/smoothoperator"
JSON_CONFIG="$CONFIG_DIR/smoothoperator.json"
ENV_CONFIG="$CONFIG_DIR/smoothoperator.env"

echo "==> Configuring systemd service..."

# Create systemd service file
cat <<EOF > /tmp/smoothoperator.service
[Unit]
Description=SmoothOperator - RabbitMQ to Liquidsoap Controller
After=network.target rabbitmq-server.service
Wants=rabbitmq-server.service

[Service]
Type=simple
User=root
Group=root
WorkingDirectory=$CONFIG_DIR
# Load environment variables from the .env file
EnvironmentFile=$ENV_CONFIG
# Run the binary with the JSON config path
ExecStart=$BIN_DIR/smoothoperator -c $JSON_CONFIG
Restart=on-failure
RestartSec=10
StandardOutput=journal
StandardError=journal
SyslogIdentifier=smoothoperator

[Install]
WantedBy=multi-user.target
EOF

sudo mv /tmp/smoothoperator.service /etc/systemd/system/smoothoperator.service
sudo systemctl daemon-reload

echo "✓ Systemd service created in /etc/systemd/system/smoothoperator.service"
echo ""
echo "==> Next steps:"
echo "    1. Edit config: sudo nano $JSON_CONFIG"
echo "    2. Edit secrets: sudo nano $ENV_CONFIG"
echo "    3. Enable service: sudo systemctl enable smoothoperator"
echo "    4. Start service: sudo systemctl start smoothoperator"

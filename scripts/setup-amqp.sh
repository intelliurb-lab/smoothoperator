#!/usr/bin/env bash
# Setup RabbitMQ para Memphis

set -euo pipefail
IFS=$'\n\t'

echo "==> Installing RabbitMQ..."

# Detect OS
if [ -f /etc/debian_version ]; then
  # Debian/Ubuntu
  echo "Detected Debian/Ubuntu"
  echo "Run: sudo apt-get install -y rabbitmq-server"
  echo "Then: sudo systemctl start rabbitmq-server"

elif [ -f /etc/redhat-release ]; then
  # RHEL/CentOS
  echo "Detected RHEL/CentOS"
  echo "Run: sudo yum install -y rabbitmq-server"
  echo "Then: sudo systemctl start rabbitmq-server"

elif [ "$(uname)" = "Darwin" ]; then
  # macOS
  echo "Detected macOS"
  echo "Run: brew install rabbitmq"
  echo "Then: brew services start rabbitmq"

else
  echo "Unknown OS. Please install RabbitMQ manually."
  exit 1
fi

echo ""
echo "==> After installing, create Memphis exchange and queue:"
echo ""
echo "sudo rabbitmqctl add_user memphis memphis123"
echo "sudo rabbitmqctl set_permissions -p / memphis '.*' '.*' '.*'"
echo ""
echo "Then run:"
echo ""
echo "rabbitmqctl declare_exchange radio.events topic"
echo "rabbitmqctl declare_queue ls.commands durable=true"
echo "rabbitmqctl bind_queue ls.commands radio.events 'control.*'"
echo "rabbitmqctl bind_queue ls.commands radio.events 'announcement.*'"
echo ""

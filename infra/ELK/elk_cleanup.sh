#!/bin/bash

# ELK Stack Cleanup Script
# Usage: ./elk_cleanup.sh [OPTIONS]
# Options:
#   --purge-images     Remove ELK Docker images
#   --purge-volumes    Remove ELK Docker volumes and persistent data
#   --complete-cleanup Complete cleanup (containers, volumes, images)
#   --help             Show this help message

show_help() {
    echo "ELK Stack Cleanup Script"
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --purge-images     Remove ELK Docker images"
    echo "  --purge-volumes    Remove ELK Docker volumes and persistent data"
    echo "  --complete-cleanup Complete cleanup (containers, volumes, images)"
    echo "  --help             Show this help message"
    echo ""
    echo "Default behavior: Stop containers and delete Elasticsearch indices only"
}

# Parse command line arguments
PURGE_IMAGES=false
PURGE_VOLUMES=false
COMPLETE_CLEANUP=false

for arg in "$@"; do
  case $arg in
    --purge-images)      PURGE_IMAGES=true ;;
    --purge-volumes)     PURGE_VOLUMES=true ;;
    --complete-cleanup)  COMPLETE_CLEANUP=true ;;
    --help)              show_help; exit 0 ;;
    *)                   echo "Unknown option: $arg"; show_help; exit 1 ;;
  esac
done

echo "Starting ELK stack cleanup..."

# Stop ELK stack containers
echo "Stopping ELK containers..."
CONTAINERS=$(docker ps --format="{{.Image}} {{.ID}}" | grep -E "(elastic|kibana|logstash|filebeat)" | cut -d' ' -f2)
if [ -n "$CONTAINERS" ]; then
    docker kill $CONTAINERS
else
    echo "No running ELK containers found"
fi

# Remove containers
echo "Removing stopped ELK containers..."
STOPPED_CONTAINERS=$(docker ps -a --format="{{.Image}} {{.ID}}" | grep -E "(elastic|kibana|logstash|filebeat)" | cut -d' ' -f2)
if [ -n "$STOPPED_CONTAINERS" ]; then
    docker rm $STOPPED_CONTAINERS 2>/dev/null
fi

# complete-cleanup option overrides individual flags
if [ "$COMPLETE_CLEANUP" = true ]; then
    PURGE_VOLUMES=true
    PURGE_IMAGES=true
    echo "complete-cleanup cleanup enabled - removing everything!"
fi

# Remove volumes if requested
if [ "$PURGE_VOLUMES" = true ]; then
    echo "Purging ELK volumes..."
    VOLUMES=$(docker volume ls -q | grep -E "(elastic|kibana|logstash|filebeat)")
    if [ -n "$VOLUMES" ]; then
        docker volume rm $VOLUMES 2>/dev/null
        echo "Volumes purged"
    else
        echo "No ELK volumes found"
    fi
fi

# Remove images if requested
if [ "$PURGE_IMAGES" = true ]; then
    echo "Purging ELK images..."
    IMAGES=$(docker images --format="{{.Repository}} {{.ID}}" | grep -E "(elastic|kibana|logstash|filebeat)" | cut -d' ' -f2)
    if [ -n "$IMAGES" ]; then
        docker rmi $IMAGES 2>/dev/null
        echo "Images purged"
    else
        echo "No ELK images found"
    fi
fi

echo "ELK cleanup completed!"
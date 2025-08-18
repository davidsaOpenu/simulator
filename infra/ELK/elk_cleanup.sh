#!/usr/bin/env bash
# ELK Cleanup (Docker + Podman)
# Usage: ./elk_cleanup.sh [--purge-images] [--purge-volumes] [--complete-cleanup] [--help]
# Notes:
# - Auto-detects engine (Podman preferred if both exist). Override via ENGINE=docker|podman env.
# - PROJECT comes from COMPOSE_PROJECT_NAME or defaults to "docker-elk".

set -euo pipefail

help() {
  cat <<EOF
ELK Cleanup
  --purge-images      Remove ELK images
  --purge-volumes     Remove ELK volumes
  --complete-cleanup  Remove containers + volumes + images
  --help              Show this help
EOF
}

PURGE_IMAGES=false; PURGE_VOLUMES=false
for a in "$@"; do
  case "$a" in
    --purge-images) PURGE_IMAGES=true ;;
    --purge-volumes) PURGE_VOLUMES=true ;;
    --complete-cleanup) PURGE_IMAGES=true; PURGE_VOLUMES=true ;;
    --help) help; exit 0 ;;
    *) echo "Unknown option: $a"; help; exit 1 ;;
  esac
done

ENGINE="${ENGINE:-}"
if [[ -z "${ENGINE}" ]]; then
  if command -v podman >/dev/null 2>&1; then ENGINE=podman
  elif command -v docker >/dev/null 2>&1; then ENGINE=docker
  else echo "ERROR: neither podman nor docker found"; exit 1; fi
fi

PROJECT="${COMPOSE_PROJECT_NAME:-docker-elk}"

# Regex for ELK images (as ERE). Keep single backslashes here (useful for grep -E).
RGX_IMG='(docker\.elastic\.co/.*/(elasticsearch|kibana|logstash|filebeat)|(^|[/:])(elasticsearch|kibana|logstash|filebeat)(:|$))'
# Make an AWK-safe copy where backslashes are doubled so awk doesn't warn/munge them.
AWK_RGX="${RGX_IMG//\\/\\\\}"

# Small helpers
uniq_lines() { sort -u | sed '/^$/d'; }

running_containers() {
  {
    $ENGINE ps -q --filter "label=com.docker.compose.project=${PROJECT}" 2>/dev/null
    $ENGINE ps -q --filter "label=io.podman.compose.project=${PROJECT}" 2>/dev/null
    $ENGINE ps -q --filter "name=${PROJECT}_" 2>/dev/null
    # Fallback: anything whose image matches our ELK regex
    $ENGINE ps -a --format '{{.ID}} {{.Image}}' | awk -v r="$AWK_RGX" '$0 ~ r {print $1}'
  } | uniq_lines
}

all_containers() {
  {
    $ENGINE ps -aq --filter "label=com.docker.compose.project=${PROJECT}" 2>/dev/null
    $ENGINE ps -aq --filter "label=io.podman.compose.project=${PROJECT}" 2>/dev/null
    $ENGINE ps -aq --filter "name=${PROJECT}_" 2>/dev/null
    $ENGINE ps -a --format '{{.ID}} {{.Image}}' | awk -v r="$AWK_RGX" '$0 ~ r {print $1}'
  } | uniq_lines
}

project_volumes() {
  $ENGINE volume ls -q | grep -E "^${PROJECT}_" || true
}

elk_images() {
  # Prefer matching by repository name; fall back covers retagged/local copies too
  $ENGINE images --format '{{.Repository}} {{.ID}}' \
    | awk -v r="$AWK_RGX" '$0 ~ r {print $2}' | uniq_lines
}

echo "[*] Engine: $ENGINE | Project: $PROJECT"

echo "[*] Stopping ELK containers..."
ids="$(running_containers || true)"
if [[ -n "${ids:-}" ]]; then
  # shellcheck disable=SC2086
  $ENGINE kill $ids >/dev/null 2>&1 || true
else
  echo "    none"
fi

echo "[*] Removing ELK containers..."
ids="$(all_containers || true)"
if [[ -n "${ids:-}" ]]; then
  # shellcheck disable=SC2086
  $ENGINE rm -f $ids >/dev/null 2>&1 || true
else
  echo "    none"
fi

if $PURGE_VOLUMES; then
  echo "[*] Removing ELK volumes..."
  vols="$(project_volumes || true)"
  if [[ -n "${vols:-}" ]]; then
    # shellcheck disable=SC2086
    $ENGINE volume rm $vols >/dev/null 2>&1 || true
  else
    echo "    none"
  fi
fi

if $PURGE_IMAGES; then
  echo "[*] Removing ELK images..."
  imgs="$(elk_images || true)"
  if [[ -n "${imgs:-}" ]]; then
    # shellcheck disable=SC2086
    $ENGINE rmi -f $imgs >/dev/null 2>&1 || true
  else
    echo "    none"
  fi
fi

echo "[*] Done."

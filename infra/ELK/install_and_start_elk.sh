#!/bin/bash

detect_container_runtime() {
  if command -v podman >/dev/null 2>&1; then
    echo "Podman detected; using Podman"
    CONTAINER_CMD="podman"
    if podman compose version >/dev/null 2>&1; then
      COMPOSE_CMD="podman compose"
    elif command -v podman-compose >/dev/null 2>&1; then
      COMPOSE_CMD="podman-compose"
    elif command -v docker-compose >/dev/null 2>&1; then
      COMPOSE_CMD="docker-compose"
      export DOCKER_HOST="unix:///run/user/$(id -u)/podman/podman.sock"
    else
      echo "Error: no compose frontend found for Podman (podman compose / podman-compose / docker-compose)"; exit 1
    fi
    COMPOSE_EXEC_T_OPT=""
  elif command -v docker >/dev/null 2>&1; then
    echo "Docker detected; using Docker"
    CONTAINER_CMD="docker"
    if docker compose version >/dev/null 2>&1; then
      COMPOSE_CMD="docker compose"
    elif command -v docker-compose >/dev/null 2>&1; then
      COMPOSE_CMD="docker-compose"
    else
      echo "Error: neither 'docker compose' nor 'docker-compose' found"; exit 1
    fi
    COMPOSE_EXEC_T_OPT="-T"
  else
    echo "Error: neither Podman nor Docker found"; exit 1
  fi
  
  export COMPOSE_PROJECT_NAME="${COMPOSE_PROJECT_NAME:-docker-elk}"

  echo "Using container runtime: $CONTAINER_CMD"
  echo "Using compose command:  $COMPOSE_CMD"
}

# Function to setup Elasticsearch built-in user passwords
setup_elasticsearch_passwords() {
  echo "Setting up Elasticsearch built-in user passwords..."

  # Wait for ES to be reachable (auth may not work yet)
  for i in $(seq 1 12); do
    if curl -k -s --connect-timeout 5 https://localhost:9200 >/dev/null; then
      break
    fi
    echo "Waiting for Elasticsearch... ($i/12)"
    sleep 5
  done
  if ! curl -k -s --connect-timeout 5 https://localhost:9200 >/dev/null; then
    echo "ERROR: Elasticsearch is not reachable"; return 1
  fi

  # Check current auth status via Security API
  elastic_ok=false
  curl -k -s -f -u "elastic:$ELASTIC_PASSWORD" \
       https://localhost:9200/_security/_authenticate >/dev/null && elastic_ok=true

  kib_ok=false
  curl -k -s -f -u "kibana_system:$KIBANA_SYSTEM_PASSWORD" \
       https://localhost:9200/_security/_authenticate >/dev/null && kib_ok=true

  if $elastic_ok && $kib_ok; then
    echo "Authentication already configured, skipping setup"
    return 0
  fi

  # Quiet the logs while changing creds
  $COMPOSE_CMD stop kibana logstash >/dev/null 2>&1 || true
  sleep 3

  if ! $elastic_ok; then
    echo "WARN: Cannot authenticate as 'elastic' with provided ELASTIC_PASSWORD."
    echo "      Skipping password changes for built-in users. Verify the password or run the repo's setup first."
  else
    # Set passwords via Security API (CLI doesn't accept -p)
    for pair in \
      "kibana_system:$KIBANA_SYSTEM_PASSWORD" \
      "logstash_system:$LOGSTASH_SYSTEM_PASSWORD" \
      "beats_system:$BEATS_SYSTEM_PASSWORD"
    do
      username="${pair%:*}"; password="${pair#*:}"
      echo "Setting password for $username..."
      curl -k -s -f -u "elastic:$ELASTIC_PASSWORD" \
           -H "Content-Type: application/json" \
           -X POST "https://localhost:9200/_security/user/$username/_password" \
           -d "{\"password\":\"$password\"}" >/dev/null \
        && echo "  ✔ $username password set" \
        || echo "  ✖ WARNING: Failed to set $username"
    done
  fi

  # Verify and restart services
  if curl -k -s -f -u "kibana_system:$KIBANA_SYSTEM_PASSWORD" \
        https://localhost:9200/_security/_authenticate >/dev/null; then
    echo "Kibana authentication verified"
  else
    echo "ERROR: Kibana authentication failed"
  fi

  $COMPOSE_CMD start kibana logstash
  echo "Password setup complete, services restarting..."
}


# Function to check Kibana health
check_kibana_health() {
    echo "Checking Kibana health on HTTPS..."
    
    for attempt in $(seq 1 30); do
        echo "Attempt $attempt/30: Checking Kibana status..."
        
        curl -k -s -f --connect-timeout 5 "https://localhost:5601/api/status" > /dev/null 2>&1 && {
            echo "Kibana is up and running on HTTPS!"
            echo "Access Kibana at: https://localhost:5601"
            echo "Default credentials: Username: elastic, Password: $ELASTIC_PASSWORD"
            echo "WARNING: Remember to change the default password for security!"
            return 0
        }
        
        echo "Kibana not ready yet, waiting 10 seconds..."
        sleep 10
    done
    
    echo "ERROR: Kibana failed to start within 300 seconds"
    echo "Check the logs with: $COMPOSE_CMD logs kibana"
    return 1
}

# Function to check Elasticsearch health
check_elasticsearch_health() {
    echo "Checking Elasticsearch health on HTTPS..."
    
    for attempt in $(seq 1 30); do
        echo "Attempt $attempt/30: Checking Elasticsearch status..."
        
        health_response=$(curl -k -s -f --connect-timeout 5 -u "elastic:$ELASTIC_PASSWORD" "https://localhost:9200/_cluster/health" 2>/dev/null) && {
            cluster_status=$(echo "$health_response" | grep -o '"status":"[^"]*"' | cut -d':' -f2 | tr -d '"')
            echo "Elasticsearch is up and running on HTTPS!"
            echo "Cluster status: $cluster_status"
            echo "Access Elasticsearch at: https://localhost:9200"
            return 0
        }
        
        echo "Elasticsearch not ready yet, waiting 10 seconds..."
        sleep 10
    done
    
    echo "ERROR: Elasticsearch failed to start within 300 seconds"
    echo "Check the logs with: $COMPOSE_CMD logs elasticsearch"
    return 1
}

# Function to check Logstash health
check_logstash_health() {
    echo "Checking Logstash health..."
    
    for attempt in $(seq 1 30); do
        echo "Attempt $attempt/30: Checking Logstash status..."
        
        health_response=$(curl -s -f --connect-timeout 5 "http://localhost:9600/_node/stats" 2>/dev/null) && {
            pipeline_status=$(echo "$health_response" | grep -o '"status":"[^"]*"' | head -1 | cut -d':' -f2 | tr -d '"')
            pipeline_workers=$(echo "$health_response" | grep -o '"workers":[0-9]*' | cut -d':' -f2)
            
            echo "Logstash is up and running!"
            [[ -n "$pipeline_status" ]] && echo "Pipeline status: $pipeline_status"
            [[ -n "$pipeline_workers" ]] && echo "Active workers: $pipeline_workers"
            echo "Logstash stats available at: http://localhost:9600"
            return 0
        }
        
        echo "Logstash not ready yet, waiting 10 seconds..."
        sleep 10
    done
    
    echo "ERROR: Logstash failed to start within 300 seconds"
    echo "Check the logs with: $COMPOSE_CMD logs logstash"
    return 1
}


# Install and start filebeat
setup_filebeat() {
    local log_dir="$1" elk_dir="$2"
    local fb_image="docker.elastic.co/beats/filebeat:${FB_IMAGE_TAG}"
    local fb_cfg="${elk_dir}/filebeat.yml"
    local cert_ca_dir="${elk_dir}/docker-elk/tls/certs/ca"
    local env_file="${elk_dir}/.env"
    local fb_data_dir="${elk_dir}/filebeat-data"

    echo "Setting up Filebeat [${FB_IMAGE_TAG}]..."

    # Validate paths
    [[ -f "$fb_cfg" ]] || { echo "filebeat.yml not found at $fb_cfg"; return 1; }
    [[ -d "$cert_ca_dir" ]] || { echo "Certificate directory not found at $cert_ca_dir"; return 1; }
    [[ -f "$env_file" ]] || { echo ".env not found at $env_file"; return 1; }

    # Load environment
    set -o allexport; source "$env_file"; set +o allexport
    : "${ELASTIC_PASSWORD:?ELASTIC_PASSWORD missing in $env_file}"

    echo "Found config: $fb_cfg | certs: $cert_ca_dir | env: $env_file"

    # Compose network name (docker-elk defines a custom 'elk' network)
    local network_name="${COMPOSE_PROJECT_NAME:-docker-elk}_elk"

    # SELinux label option (for RHEL/Fedora/etc.)
    local SELINUX_LABEL=""
    if command -v getenforce >/dev/null 2>&1; then
        local se; se="$(getenforce 2>/dev/null || echo Disabled)"
        [[ "$se" != "Disabled" ]] && SELINUX_LABEL="Z"   # we'll combine as subopts
    fi

    # Build mount suboptions
    # - certs & config mounts are read-only: "Z,ro" if SELinux; otherwise "ro"
    local ro_subopts="ro"
    [[ -n "$SELINUX_LABEL" ]] && ro_subopts="$SELINUX_LABEL,$ro_subopts"

    # - data mount must be writable; add Podman UID shift and SELinux label when needed
    local data_subopts=""
    if [[ "$CONTAINER_CMD" == "podman" ]]; then
        data_subopts="U"
        [[ -n "$SELINUX_LABEL" ]] && data_subopts="Z,$data_subopts"
    else
        [[ -n "$SELINUX_LABEL" ]] && data_subopts="Z"
    fi
    local data_opts=""
    [[ -n "$data_subopts" ]] && data_opts=":$data_subopts"

    # Ensure data dir exists and is writable; fix ownership for Podman rootless
    mkdir -p "$fb_data_dir"
    if [[ "$CONTAINER_CMD" == "podman" ]] && command -v podman >/dev/null 2>&1; then
        # Map ownership into the user namespace so container root can write
        podman unshare chown -R 0:0 "$fb_data_dir" 2>/dev/null || true
    fi

    # Pull image & remove any existing container
    "$CONTAINER_CMD" pull "$fb_image"
    "$CONTAINER_CMD" ps -aq -f name=filebeat | xargs -r "$CONTAINER_CMD" rm -f

    # One-time setup (no data mount needed)
    echo "Running one-time setup"
    "$CONTAINER_CMD" run --rm --network "$network_name" --env-file "$env_file" \
        -v "$cert_ca_dir:/usr/share/filebeat/certs/ca:$ro_subopts" \
        "$fb_image" setup -e --index-management --pipelines --dashboards \
        -E "setup.xpack.ml.enabled=false" \
        -E "setup.kibana.host=kibana:5601" \
        -E "setup.kibana.protocol=https" \
        -E "setup.kibana.username=elastic" \
        -E "setup.kibana.password=${ELASTIC_PASSWORD}" \
        -E "setup.kibana.ssl.certificate_authorities=[/usr/share/filebeat/certs/ca/ca.crt]" \
        -E "output.elasticsearch.hosts=[https://elasticsearch:9200]" \
        -E "output.elasticsearch.username=elastic" \
        -E "output.elasticsearch.password=${ELASTIC_PASSWORD}" \
        -E "output.elasticsearch.ssl.certificate_authorities=[/usr/share/filebeat/certs/ca/ca.crt]" \
        || { echo "Setup failed"; return 1; }

    echo "Starting Filebeat daemon"
    if [[ "$CONTAINER_CMD" == "docker" ]]; then
        # Docker: include Docker-specific mounts (socket + container logs) if you need autodiscover
        "$CONTAINER_CMD" run -d --name filebeat --restart unless-stopped --network "$network_name" \
            --env-file "$env_file" \
            -v "$fb_cfg:/usr/share/filebeat/filebeat.yml:$ro_subopts" \
            -v "$cert_ca_dir:/usr/share/filebeat/certs/ca:$ro_subopts" \
            -v "$log_dir:/logs:$ro_subopts" \
            -v "/var/lib/docker/containers:/var/lib/docker/containers:ro" \
            -v "/var/run/docker.sock:/var/run/docker.sock:ro" \
            -v "$fb_data_dir:/usr/share/filebeat/data${data_opts}" \
            "$fb_image" filebeat -e --strict.perms=false -c /usr/share/filebeat/filebeat.yml
    else
        # Podman: drop Docker-specific mounts (use host log dir only)
        "$CONTAINER_CMD" run -d --name filebeat --restart unless-stopped --network "$network_name" \
            --env-file "$env_file" \
            -v "$fb_cfg:/usr/share/filebeat/filebeat.yml:$ro_subopts" \
            -v "$cert_ca_dir:/usr/share/filebeat/certs/ca:$ro_subopts" \
            -v "$log_dir:/logs:$ro_subopts" \
            -v "$fb_data_dir:/usr/share/filebeat/data${data_opts}" \
            "$fb_image" filebeat -e --strict.perms=false -c /usr/share/filebeat/filebeat.yml
    fi

    echo "Filebeat ${FB_IMAGE_TAG} is up"
}

# Add or Update Kibana runtime field from a Painless script
add_kibana_runtime_field() {
  local script_file="${1:-type_friendly_name.painless}"     # Painless file path
  local field_name="${2:-type_friendly_name}"   # Runtime field name
  local kibana="https://localhost:5601"         # Kibana base URL
  local creds="elastic:${ELASTIC_PASSWORD:-}"   # Basic auth (elastic)

  # --- Prerequisites ---
  command -v curl >/dev/null || { echo "ERROR: curl missing"; return 1; }
  command -v jq   >/dev/null || { echo "ERROR: jq missing"; return 1; }
  [[ -n "${ELASTIC_PASSWORD:-}" ]] || { echo "ERROR: ELASTIC_PASSWORD not set"; return 1; }
  [[ -f "$script_file" ]] || { echo "ERROR: file not found: $script_file"; return 1; }

  # --- Find all Filebeat data views ---
  local so_url list
  so_url="$kibana/api/saved_objects/_find?type=index-pattern&per_page=10000"
  list="$(curl -s -k -u "$creds" -H 'kbn-xsrf: true' "$so_url" \
          | jq -r '.saved_objects[]
                    | select(.attributes.title | test("^filebeat.*"; "i"))
                    | "\(.id)|\(.attributes.title)"')"
  [[ -n "$list" ]] || { echo "ERROR: no filebeat data views found"; return 1; }

  # --- Build request payloads ---
  local create_payload update_payload
  create_payload="$(jq -Rs --arg name "$field_name" \
                    '{name:$name, runtimeField:{type:"keyword", script:{source:.}}}' \
                    "$script_file")"
  update_payload="$(jq -Rs \
                    '{runtimeField:{type:"keyword", script:{source:.}}}' \
                    "$script_file")"

  # --- Temp response file for HTTP bodies ---
  local resp code entry id title
  resp="$(mktemp)"; trap 'rm -f "$resp"' RETURN

  # --- Create/update the field on each data view ---
  IFS=$'\n'
  for entry in $list; do
    id="${entry%%|*}"
    title="${entry#*|}"

    # Try create
    code="$(curl -s -k -u "$creds" -H 'kbn-xsrf: true' -H 'Content-Type: application/json' \
             -o "$resp" -w '%{http_code}' -X POST \
             "$kibana/api/data_views/data_view/$id/runtime_field" \
             --data-binary "$create_payload" || true)"
    if [[ "$code" =~ ^20[01]$ ]]; then
      echo "OK: $title (created)"
      continue
    fi

    # If already exists, update
    if [[ "$code" == "409" ]]; then
      code="$(curl -s -k -u "$creds" -H 'kbn-xsrf: true' -H 'Content-Type: application/json' \
               -o "$resp" -w '%{http_code}' -X PUT \
               "$kibana/api/data_views/data_view/$id/runtime_field/$field_name" \
               --data-binary "$update_payload" || true)"
      if [[ "$code" =~ ^20[0-9]$ ]]; then
        echo "OK: $title (updated)"
        continue
      fi
    fi

    # Minimal error line
    local msg; msg="$(jq -r '.message // .error?.reason // .error // empty' < "$resp")"
    [[ -n "$msg" ]] && echo "ERR: $title - HTTP $code - $msg" || echo "ERR: $title - HTTP $code"
  done
  unset IFS
}

# Get user input
if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <log_directory> <elk_directory> - Please provide the full path"
    echo "  log_directory: Directory containing the log files to monitor"
    echo "  elk_directory: Directory where ELK stack will be installed"
    echo ""
    echo "Example: $0 /../os_workshop/logs /../os_workshop/simulator/infra/ELK"
    echo ""
    echo "Make sure you have a .env file in the current directory with the following variables:"
    echo "  FB_IMAGE_TAG=8.14.2"
    echo "  DOCKER_ELK_BRANCH=tls"
    echo "  ELASTIC_PASSWORD=changeme"
    echo "  KIBANA_SYSTEM_PASSWORD=changeme"
    echo "  LOGSTASH_SYSTEM_PASSWORD=changeme"
    echo "  BEATS_SYSTEM_PASSWORD=changeme"
    echo "  ES_HEAP=512"
    exit 1
fi

# Validate log directory
if [[ ! -d "$1" ]]; then
    echo "Error: Log directory '$1' does not exist or is not a directory"
    exit 1
fi

# Validate ELK directory
if [[ ! -d "$2" ]]; then
    echo "Error: ELK directory '$2' does not exist or is not a directory"
    exit 1
fi

# Set variables
LOG_DIR="$1"
ELK_DIR="$2"

echo "Log directory: $LOG_DIR"
echo "ELK directory: $ELK_DIR"

# Change to ELK directory
cd "$ELK_DIR"

# Load environment variables from .env file
if [[ -f .env ]]; then
    echo "Loading environment variables from .env file..."
    set -o allexport
    source .env
    set +o allexport
else
    echo "Warning: .env file not found, using default values"
fi

# Define constant values (With defaults)
FB_IMAGE_TAG="${FB_IMAGE_TAG:-8.14.2}"
DOCKER_ELK_TAG="${DOCKER_ELK_TAG:-9.2505.1}"
DOCKER_ELK_BRANCH="${DOCKER_ELK_BRANCH:-tls}"
ELASTIC_PASSWORD="${ELASTIC_PASSWORD:-changeme}"
KIBANA_SYSTEM_PASSWORD="${KIBANA_SYSTEM_PASSWORD:-changeme}"
LOGSTASH_SYSTEM_PASSWORD="${LOGSTASH_SYSTEM_PASSWORD:-changeme}"
BEATS_SYSTEM_PASSWORD="${BEATS_SYSTEM_PASSWORD:-changeme}"
ES_HEAP="${ES_HEAP:-512}"

# ---- Container runtime & compose detection ----
CONTAINER_CMD=""
COMPOSE_CMD=""
COMPOSE_EXEC_T_OPT=""

detect_container_runtime

DOCKER_ELK_REPO="https://github.com/deviantony/docker-elk.git"

if [[ -d docker-elk ]]; then
  echo "[*] docker-elk directory already exists"
  cd docker-elk

  # Make sure we have the branch locally and switch to it
  git fetch origin "$DOCKER_ELK_BRANCH"
  current_branch=$(git rev-parse --abbrev-ref HEAD)

  if [[ "$current_branch" != "$DOCKER_ELK_BRANCH" ]]; then
    echo "[*] Switching from $current_branch to $DOCKER_ELK_BRANCH"
    git checkout "$DOCKER_ELK_BRANCH"
  fi

  echo "[*] Pulling latest commits for $DOCKER_ELK_BRANCH"
  git pull --ff-only origin "$DOCKER_ELK_BRANCH"
else
  echo "[*] Cloning $DOCKER_ELK_BRANCH branch of docker-elk"
  git clone --branch "$DOCKER_ELK_BRANCH" --single-branch "$DOCKER_ELK_REPO"
  cd docker-elk
fi

# Pin to specific Elastic Stack version by updating .env file
echo "[*] Setting Elastic Stack version to $DOCKER_ELK_TAG in .env file"
if [[ -f .env ]]; then
  # Update existing STACK_VERSION in .env file
  if grep -q "^STACK_VERSION=" .env; then
    sed -i "s/^STACK_VERSION=.*/STACK_VERSION=${DOCKER_ELK_TAG}/" .env
    echo "[*] Updated STACK_VERSION to $DOCKER_ELK_TAG in existing .env file"
  else
    # Add STACK_VERSION if it doesn't exist
    echo "STACK_VERSION=${DOCKER_ELK_TAG}" >> .env
    echo "[*] Added STACK_VERSION=$DOCKER_ELK_TAG to .env file"
  fi
else
  echo "[*] Creating .env file with STACK_VERSION=$DOCKER_ELK_TAG"
  echo "STACK_VERSION=${DOCKER_ELK_TAG}" > .env
fi

# Ensure localhost traffic skips any proxy
export no_proxy=localhost,127.0.0.0,127.0.1.1,local.home,${no_proxy:-}

# Check if JVM heap has already been increased
current_xmx=$(grep -o "Xmx[0-9]*m" docker-compose.yml | head -1)
current_xms=$(grep -o "Xms[0-9]*m" docker-compose.yml | head -1)
target_heap="${ES_HEAP}m"

# Increase JVM heap to configured value
if [[ "$current_xmx" == "Xmx${target_heap}" && "$current_xms" == "Xms${target_heap}" ]]; then
    echo "JVM heap already configured to ${target_heap}"
else
    echo "Updating JVM heap size to ${target_heap}..."
    sed -i "s/Xmx[0-9]*m/Xmx${target_heap}/g" docker-compose.yml
    sed -i "s/Xms[0-9]*m/Xms${target_heap}/g" docker-compose.yml
    echo "JVM heap size updated to ${target_heap}"
fi

# Check if TLS is already enabled in kibana.yml
if grep -q "server.ssl.enabled: true" kibana/config/kibana.yml; then
    echo "TLS already enabled for Kibana"
else
    echo "Enabling TLS for Kibana..."
    sed -i 's/server.ssl.enabled: false/server.ssl.enabled: true/g' kibana/config/kibana.yml
    echo "TLS enabled for Kibana"
fi

# Check if containers are already running
running_containers=$($COMPOSE_CMD ps 2>/dev/null | grep -E "Up|running" | wc -l)
total_containers=$($COMPOSE_CMD ps 2>/dev/null | awk 'NR>1' | wc -l)

if [[ $running_containers -gt 0 ]]; then
    echo "Found $running_containers/$total_containers containers already running"
    echo "Current container status:"
    $COMPOSE_CMD ps
    
    read -p "Do you want to restart the ELK stack? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "Stopping existing containers..."
        $COMPOSE_CMD down
        echo "Restarting ELK stack..."
    else
        echo "Skipping container restart, checking health..."
        check_kibana_health
        exit 0
    fi
fi

# Check if TLS certificates exist
if [[ -f "tls/certs/ca/ca.crt" ]]; then
    echo "TLS certificates already exist, skipping generation..."
else
    echo "Generating TLS certificates..."
    $COMPOSE_CMD up tls
fi

# Check if Elasticsearch setup has been completed
if $CONTAINER_CMD volume ls | grep -q "docker-elk_elasticsearch"; then
    echo "Elasticsearch volume exists, skipping setup..."
else
    echo "Setting up Elasticsearch"
    $COMPOSE_CMD up setup
fi

echo "Starting ELK stack..."
$COMPOSE_CMD up -d

check_elasticsearch_health || { echo "Elasticsearch health check failed, exiting"; exit 1; }
setup_elasticsearch_passwords || { echo "Password setup failed, exiting"; exit 1; }
check_logstash_health || { echo "Logstash health check failed, exiting"; exit 1; }
check_kibana_health || { echo "Kibana health check failed, exiting"; exit 1; }

# Set up Filebeat
cd ..
chmod go-w filebeat.yml
setup_filebeat "$LOG_DIR" "$ELK_DIR" || { echo "Filebeat setup failed, exiting"; exit 1; }

# Upload Dashboard
add_kibana_runtime_field
curl -k -X POST "https://localhost:5601/api/saved_objects/_import?overwrite=true" -H "kbn-xsrf: true" --form file=@eVSSIM_Logs_Dashboard.ndjson -u elastic:$ELASTIC_PASSWORD

# Display container status
echo ""
echo "Final Container Status:"
$CONTAINER_CMD ps
echo ""
echo "Access URLs:"
echo "  Kibana: https://localhost:5601 (elastic/$ELASTIC_PASSWORD)"
echo "  Elasticsearch: https://localhost:9200"
echo "  Logstash: http://localhost:9600"
#!/bin/bash

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

# Function to setup Elasticsearch built-in user passwords
setup_elasticsearch_passwords() {
    echo "Setting up Elasticsearch built-in user passwords..."
    
    # Wait for Elasticsearch to be ready
    local count=0
    while [ $count -lt 12 ]; do
        curl -k -s -f --connect-timeout 5 -u "elastic:$ELASTIC_PASSWORD" "https://localhost:9200/_cluster/health" > /dev/null 2>&1 && break
        echo "Waiting for Elasticsearch... ($((count+1))/12)"
        sleep 5 && ((count++))
    done
    
    # Check if authentication already works
    if curl -k -s -f -u "elastic:$ELASTIC_PASSWORD" "https://localhost:9200/_cluster/health" > /dev/null 2>&1 && \
       curl -k -s -f -u "kibana_system:$KIBANA_SYSTEM_PASSWORD" "https://localhost:9200/_cluster/health" > /dev/null 2>&1; then
        echo "Authentication already configured, skipping setup"
        return 0
    fi
    
    # Stop dependent services temporarily
    docker compose stop kibana logstash && sleep 5
    
    # Setup passwords based on authentication status
    if curl -k -s -f "https://localhost:9200/_cluster/health" > /dev/null 2>&1; then
        # Fresh install - use automatic setup
        echo "Fresh installation detected, using automatic password setup..."
        docker compose exec -T elasticsearch bin/elasticsearch-setup-passwords auto --batch > /tmp/es_passwords.txt 2>&1 || {
            echo "Auto setup failed, setting elastic password manually..."
            docker compose exec -T elasticsearch bin/elasticsearch-reset-password -u elastic -p "$ELASTIC_PASSWORD" --batch -s
        }
    fi
    
    # Set individual user passwords
    for user in "kibana_system:$KIBANA_SYSTEM_PASSWORD" "logstash_system:$LOGSTASH_SYSTEM_PASSWORD" "beats_system:$BEATS_SYSTEM_PASSWORD"; do
        username="${user%:*}"
        password="${user#*:}"
        echo "Setting password for $username..."
        
        docker compose exec -T elasticsearch bin/elasticsearch-reset-password -u "$username" -p "$password" --batch -s 2>/dev/null || \
        curl -X POST "https://localhost:9200/_security/user/$username/_password" \
          -H "Content-Type: application/json" -u "elastic:$ELASTIC_PASSWORD" -k -s \
          -d "{\"password\": \"$password\"}" > /dev/null 2>&1 || \
        echo "Warning: Could not set $username password"
    done
    
    # Verify and restart services
    curl -k -s -f -u "kibana_system:$KIBANA_SYSTEM_PASSWORD" "https://localhost:9200/_cluster/health" > /dev/null 2>&1 && \
        echo "Kibana authentication verified" || echo "ERROR: Kibana authentication failed"
    
    docker compose start kibana logstash
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
    echo "Check the logs with: docker compose logs kibana"
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
    echo "Check the logs with: docker compose logs elasticsearch"
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
    echo "Check the logs with: docker compose logs logstash"
    return 1
}

# Install and start filebeat
setup_filebeat() {
    local log_dir="$1" elk_dir="$2" 
    local fb_image="docker.elastic.co/beats/filebeat:${FB_IMAGE_TAG}"
    local fb_cfg="${elk_dir}/filebeat.yml" cert_ca_dir="${elk_dir}/docker-elk/tls/certs/ca"
    local env_file="${elk_dir}/.env" fb_data_dir="${elk_dir}/filebeat-data"
    
    echo "Setting up Filebeat [${FB_IMAGE_TAG}]..."
    
    # Validate paths
    [[ -f "$fb_cfg" ]] || { echo "filebeat.yml not found at $fb_cfg"; return 1; }
    [[ -d "$cert_ca_dir" ]] || { echo "Certificate directory not found at $cert_ca_dir"; return 1; }
    [[ -f "$env_file" ]] || { echo ".env not found at $env_file"; return 1; }
    
    # Load environment
    set -o allexport; source "$env_file"; set +o allexport
    : "${ELASTIC_PASSWORD:?ELASTIC_PASSWORD missing in $env_file}"
    
    echo "Found config: $fb_cfg | certs: $cert_ca_dir | env: $env_file"
    
    # Setup image and remove old container
    docker pull "$fb_image"
    docker ps -aq -f name=^filebeat$ | xargs -r docker rm -f
    
    # Common connection settings
    local kibana_opts="-E setup.kibana.host=kibana:5601 -E setup.kibana.protocol=https -E setup.kibana.username=elastic -E setup.kibana.password=${ELASTIC_PASSWORD} -E setup.kibana.ssl.certificate_authorities=[/usr/share/filebeat/certs/ca/ca.crt]"
    local es_opts="-E output.elasticsearch.hosts=[https://elasticsearch:9200] -E output.elasticsearch.username=elastic -E output.elasticsearch.password=${ELASTIC_PASSWORD} -E output.elasticsearch.ssl.certificate_authorities=[/usr/share/filebeat/certs/ca/ca.crt]"
    local ml_flag="setup.${FB_IMAGE_TAG%%.*:+xpack.}ml.enabled=false"
    
    echo "Running one-time setup"
    docker run --rm --network docker-elk_elk --env-file "$env_file" \
        -v "$cert_ca_dir:/usr/share/filebeat/certs/ca:ro" \
        "$fb_image" setup -e --index-management --pipelines --dashboards \
        -E "$ml_flag" $kibana_opts $es_opts || { echo "Setup failed"; return 1; }
    
    echo "Starting Filebeat daemon"
    mkdir -p "$fb_data_dir"
    docker run -d --name filebeat --restart unless-stopped --network docker-elk_elk \
        --env-file "$env_file" \
        -v "$fb_cfg:/usr/share/filebeat/filebeat.yml:ro" \
        -v "$cert_ca_dir:/usr/share/filebeat/certs/ca:ro" \
        -v "$log_dir:/logs:ro" \
        -v "/var/lib/docker/containers:/var/lib/docker/containers:ro" \
        -v "/var/run/docker.sock:/var/run/docker.sock:ro" \
        -v "$fb_data_dir:/usr/share/filebeat/data" \
        "$fb_image" filebeat -e --strict.perms=false -c /usr/share/filebeat/filebeat.yml
    
    echo "Filebeat ${FB_IMAGE_TAG} is up"
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
running_containers=$(docker compose ps --services --filter "status=running" 2>/dev/null | wc -l)
total_containers=$(docker compose ps --services 2>/dev/null | wc -l)

if [[ $running_containers -gt 0 ]]; then
    echo "Found $running_containers/$total_containers containers already running"
    echo "Current container status:"
    docker compose ps
    
    read -p "Do you want to restart the ELK stack? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "Stopping existing containers..."
        docker compose down
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
    docker compose up tls
fi

# Check if Elasticsearch setup has been completed
if docker volume ls | grep -q "docker-elk_elasticsearch"; then
    echo "Elasticsearch volume exists, skipping setup..."
else
    echo "Setting up Elasticsearch"
    docker compose up setup
fi

echo "Starting ELK stack..."
docker compose up -d

check_elasticsearch_health
setup_elasticsearch_passwords
check_logstash_health
check_kibana_health

# Set up Filebeat
setup_filebeat "$LOG_DIR" "$ELK_DIR"

# Upload Dashboard
curl -k -X POST "https://localhost:5601/api/saved_objects/_import" -H "kbn-xsrf: true" -H "securitytenant: global" --form file=@real_logs_dashboard.ndjson -u elastic:$ELASTIC_PASSWORD

# Display container status
echo ""
echo "Final Container Status:"
docker compose ps
echo ""
echo "Access URLs:"
echo "  Kibana: https://localhost:5601 (elastic/$ELASTIC_PASSWORD)"
echo "  Elasticsearch: https://localhost:9200"
echo "  Logstash: http://localhost:9600"
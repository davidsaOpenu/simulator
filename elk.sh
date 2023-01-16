#!/bin/bash
set -e

declare -x ELASTICSEARCH_DOCKER_UUID
declare -x KIBANA_DOCKER_UUID
declare -x FILEBEAT_DOCKER_UUID

# Verify environment is loaded
if [ -z $EVSSIM_ENVIRONMENT ]; then
    echo "ERROR Builder not running in evssim environment. Please execute 'source ./env.sh' first"
    exit 1
fi

# Running as root is not supported
if [ "$UID" -eq "0" ]; then
    echo "ERROR Running as root is not supported"
    exit 1
fi

# Warn if environment changed
if [ -z $EVSSIM_ENV_PATH ]; then
    echo "ERORR Missing environment file path"
    exit 1
elif [[ $(md5sum $EVSSIM_ENV_PATH | cut -d " " -f 1) != $EVSSIM_ENV_HASH ]]; then
    echo "WARNING Environment file hash changed. Please reload using 'source ./env.sh'"
fi

# Check for docker support
if ! which docker >/dev/null; then
    echo "ERORR Missing docker configuration"
    exit 1
fi

if ! docker ps 2>/dev/null >/dev/null; then
    echo "ERORR Docker has no permissions. Consider adding user to docker group. Logout and login afterwards."
    echo "      $ sudo groupadd docker"
    echo "      $ sudo usermod -aG docker $USER"
    exit 1
fi

# Configure docker use of tty if one is available
docker_extra_tty=""
if [ -t 0 ]; then
    docker_extra_tty=-t
fi

# Change to project root
pushd "$EVSSIM_ROOT_PATH"

# Create output folders (Might already exist)
for folder in $EVSSIM_CREATE_FOLDERS; do
    if [ ! -d "$folder" ]; then mkdir "$folder"; fi
done

popd

evssim_elk_run_elasticsearch() {
    export ELASTICSEARCH_DOCKER_UUID=$(\
            docker run --rm --publish 9200 \
            --env discovery.type=single-node --env xpack.security.enabled=false \
            --detach $ELK_ELASTICSEARCH_IMAGE\
    )
    export ELK_ELASTICSEARCH_EXTERNAL_PORT=$(docker port $ELASTICSEARCH_DOCKER_UUID 9200 | grep "0.0.0.0" | cut -d: -f2)
}

evssim_elk_run_kibana() {
    export KIBANA_DOCKER_UUID=$(\
            docker run --rm --publish 5601 \
            --env ELASTICSEARCH_HOSTS="http://host.docker.internal:$ELK_ELASTICSEARCH_EXTERNAL_PORT" \
            --add-host=host.docker.internal:host-gateway \
            --detach $ELK_KIBANA_IMAGE\
    )
    export ELK_KIBANA_EXTERNAL_PORT=$(docker port $KIBANA_DOCKER_UUID 5601 | grep "0.0.0.0" | cut -d: -f2)
}

evssim_elk_run_filebeat() {
	# Create or reset registry files for filebeat
	if [ -e "${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}/lastread.txt" ]; then rm "${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}/lastread.txt"; fi
	if [ -e "${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}/log.json" ]; then rm "${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}/log.json"; fi
	touch "${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}/log.json"
	if [ -e "${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}/meta.json" ]; then rm "${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}/meta.json"; fi
	touch "${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}/meta.json"
	echo "{\"version\":\"1\"}" >> "${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}/meta.json"
	
	export FILEBEAT_DOCKER_UUID=$(\
            docker run --rm --env ELK_ELASTICSEARCH_EXTERNAL_PORT \
            --env ELK_ELASTICSEARCH_HOSTNAME=host.docker.internal \
            --volume="${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}/log.json:/usr/share/filebeat/data/registry/filebeat/log.json" \
            --volume="${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}/meta.json:/usr/share/filebeat/data/registry/filebeat/meta.json" \
            --volume="${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}:/logs/" \
            --volume="${ELK_FILEBEAT_CONF_PATH}:/usr/share/filebeat/filebeat.yml:ro" \
            --add-host=host.docker.internal:host-gateway \
            --detach $ELK_FILEBEAT_IMAGE \
            filebeat -e --strict.perms=false\
    )
}

evssim_elk_run_stack() {
    evssim_elk_run_elasticsearch
    evssim_elk_run_kibana
    evssim_elk_run_filebeat
}

evssim_elk_stop_stack() {
    for container_uuid in $(echo $ELASTICSEARCH_DOCKER_UUID $KIBANA_DOCKER_UUID $FILEBEAT_DOCKER_UUID); do
        if [ ! -z $container_uuid ]; then
            if docker ps -q --no-trunc | grep $container_uuid > /dev/null; then
                docker stop $container_uuid;
            fi
        fi
    done

    export ELASTICSEARCH_DOCKER_UUID=""
    export KIBANA_DOCKER_UUID=""
    export FILEBEAT_DOCKER_UUID=""
}

evssim_elk_build_test_image() {
    docker build $EVSSIM_ROOT_PATH/$EVSSIM_ELK_FOLDER/tests -t $ELK_TESTS_IMAGE
}

evssim_elk_run_tests() {
    # Copy samples
    rm -rf ${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}/*.log
    cp -a $EVSSIM_ROOT_PATH/$EVSSIM_ELK_FOLDER/sample/* ${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}

    docker run --rm \
        --volume="$EVSSIM_ROOT_PATH/$EVSSIM_ELK_FOLDER/sample/:/logs/" \
        --add-host=host.docker.internal:host-gateway \
        $ELK_TESTS_IMAGE \
        --logs-directory /logs/ \
        --elasticsearch-host host.docker.internal \
        --elasticsearch-port $ELK_ELASTICSEARCH_EXTERNAL_PORT \
        --elasticsearch-index-template filebeat-*
}


#!/bin/bash

IMAGE_NAME=ubuntu-24.04-server-cloudimg-amd64-podman.img

# Download the image (bypass proxy)
# wget --no-proxy http://192.114.0.189/$IMAGE_NAME

# Copy the image
cp /home/davidsa/public_html/$IMAGE_NAME .

# Function to find free port
find_free_port() {
    local port
    for port in {2224..2299}; do
        if ! netstat -tuln | grep -q ":$port "; then
            echo $port
            return
        fi
    done 
    echo 2224  # fallback port
}

# Get free TCP port
free_tcp_port=$(find_free_port)
echo "Using port: $free_tcp_port"

# Store variables for cleanup stage
echo $free_tcp_port > vm_port.txt
echo $IMAGE_NAME > vm_image.txt

# Start QEMU VM in background
qemu-system-x86_64 -enable-kvm -cpu host -m 4096 -smp 4 -hda $IMAGE_NAME \
    -nic user,hostfwd=tcp::$free_tcp_port-:22 -nographic &
VM_PID=$!
echo $VM_PID > vm_pid.txt
echo "Started VM with PID: $VM_PID"

# Give VM a moment to start
sleep 10

# Check if VM process is still running
if ! kill -0 $VM_PID 2>/dev/null; then
    echo "ERROR: VM process failed to start or crashed immediately"
    echo "VM PID $VM_PID is not running"
    exit 1
fi
echo "VM process is running, waiting for SSH connectivity..."

# Wait for VM to launch (check SSH connectivity)
echo "Waiting for VM to be ready..."
for i in {1..60}; do
    if ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no -p $free_tcp_port elk@localhost "echo VM is ready" 2>/dev/null; then
        echo "VM is ready after $i attempts"
        break
    fi
    echo "Attempt $i: VM not ready yet, waiting..."
    sleep 10
done

# Copy simulator folder to the VM 
echo "Copying simulator folder to VM..."
scp -o StrictHostKeyChecking=no -P $free_tcp_port -r simulator elk@localhost:~/simulator

echo "Copying logs folder to VM..."
scp -o StrictHostKeyChecking=no -P $free_tcp_port -r logs elk@localhost:~/logs

echo "Print with ls -R"
ssh -o StrictHostKeyChecking=no -p $free_tcp_port elk@localhost "ls -R"

# Execute podman command on the VM and show output
echo "Testing podman on VM..."
echo "==================== PODMAN OUTPUT ===================="
ssh -o StrictHostKeyChecking=no -p $free_tcp_port elk@localhost "podman run hello-world" || echo "Podman command failed"
echo "======================= END OUTPUT ====================="
echo "==================== START ELK ======================"
ssh -o StrictHostKeyChecking=no -p $free_tcp_port elk@localhost "cd ~/simulator/infra/ELK/; ./install_and_start_elk.sh ../../../logs ../ELK"
echo "======================= END OUTPUT ====================="

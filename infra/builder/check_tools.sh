check_tools() {
    local tools=("docker" "docker-compose" "ansible" "tox")

    echo "Checking required tools..."

    for tool in "${tools[@]}"; do
        if command -v "$tool" &>/dev/null; then
            local version
            version=$("$tool" --version 2>&1 | head -n1)
            echo "Success. $tool — $version"
        else
            echo "Failed. Please install $tool — NOT installed"
            exit 1
        fi
    done

}

check_tools


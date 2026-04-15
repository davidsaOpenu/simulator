check_tools() {
    local tools=("docker" "docker-compose" "ansible" "tox")
    local all_installed=true

    echo "🔍 Checking required tools..."
    echo "================================"

    for tool in "${tools[@]}"; do
        if command -v "$tool" &>/dev/null; then
            local version
            version=$("$tool" --version 2>&1 | head -n1)
            echo "✅ $tool — $version"
        else
            echo "❌ $tool — NOT installed"
            all_installed=false
        fi
    done

    echo "================================"
    if $all_installed; then
        echo "✅ All tools are installed."
        return 0
    else
        echo "⚠️  Some tools are missing. Please install them."
        return 1
    fi
}

check_tools


#!/bin/bash

# ============================================
# Virtualization Environment Setup Script
# Version: 1.0
# Author: System Administrator
# ============================================

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running as root
check_root() {
    if [[ $EUID -eq 0 ]]; then
        log_warning "Running as root user"
    else
        log_info "Running as $(whoami)"
    fi
}

# Update package lists
update_packages() {
    log_info "Updating package lists..."
    sudo apt update
    if [ $? -eq 0 ]; then
        log_success "Package lists updated successfully"
    else
        log_error "Failed to update package lists"
        exit 1
    fi
}

# Install development tools
install_dev_tools() {
    local packages=(
        "g++"
        "build-essential"
        "cmake"
        "pkg-config"
    )
    
    log_info "Installing development tools..."
    for pkg in "${packages[@]}"; do
        log_info "Installing $pkg..."
        sudo apt install -y "$pkg"
    done
    log_success "Development tools installed successfully"
}

# Install virtualization packages
install_virtualization() {
    local packages=(
        "qemu-kvm"
        "libvirt-daemon-system"
        "libvirt-clients"
        "bridge-utils"
        "virt-manager"
        "qemu-utils"
        "libvirt-dev"
        "libvirt0"
    )
    
    log_info "Installing virtualization packages..."
    for pkg in "${packages[@]}"; do
        log_info "Installing $pkg..."
        sudo apt install -y "$pkg"
    done
    log_success "Virtualization packages installed successfully"
}

# Install additional dependencies
install_additional_deps() {
    local packages=(
        "libxml2-dev"
        "libnl-3-dev"
        "libnl-route-3-dev"
    )
    
    log_info "Installing additional dependencies..."
    for pkg in "${packages[@]}"; do
        log_info "Installing $pkg..."
        sudo apt install -y "$pkg"
    done
    log_success "Additional dependencies installed successfully"
}

# Configure libvirt
configure_libvirt() {
    log_info "Configuring libvirt..."
    
    # Add user to libvirt group
    sudo usermod -aG libvirt $(whoami)
    sudo usermod -aG kvm $(whoami)
    
    # Start and enable libvirt service
    sudo systemctl enable libvirtd
    sudo systemctl start libvirtd
    
    log_success "Libvirt configured successfully"
}

# Verify installations
verify_installation() {
    log_info "Verifying installations..."
    
    # Check if commands are available
    commands=("g++" "cmake" "virsh" "qemu-system-x86_64")
    for cmd in "${commands[@]}"; do
        if command -v $cmd &> /dev/null; then
            log_success "$cmd is installed"
        else
            log_error "$cmd is not installed"
        fi
    done
    
    # Check libvirt service status
    if systemctl is-active --quiet libvirtd; then
        log_success "Libvirt service is running"
    else
        log_error "Libvirt service is not running"
    fi
}

# Clean up
cleanup() {
    log_info "Cleaning up..."
    sudo apt autoremove -y
    sudo apt clean
    log_success "Cleanup completed"
}

# Main execution
main() {
    log_info "Starting virtualization environment setup..."
    log_info "Date: $(date)"
    log_info "System: $(uname -a)"
    
    check_root
    update_packages
    install_dev_tools
    install_virtualization
    install_additional_deps
    configure_libvirt
    verify_installation
    cleanup
    
    log_success "============================================"
    log_success "Setup completed successfully!"
    log_success "Please logout and login again for group changes to take effect"
    log_success "============================================"
    
    echo -e "\nNext steps:"
    echo "1. Logout and login again"
    echo "2. Run 'virsh list --all' to verify libvirt"
    echo "3. Run 'virt-manager' to open virtualization manager"
}

# Handle script arguments
case "${1:-}" in
    "--help" | "-h")
        echo "Usage: $0 [OPTION]"
        echo "Options:"
        echo "  -h, --help     Show this help message"
        echo "  -v, --verify   Only verify current installation"
        echo "  --clean        Only clean up system"
        ;;
    "--verify" | "-v")
        verify_installation
        ;;
    "--clean")
        cleanup
        ;;
    *)
        # Confirm before proceeding
        echo -e "${YELLOW}This script will install virtualization tools and development packages.${NC}"
        read -p "Do you want to continue? (y/N): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            main
        else
            log_info "Installation cancelled"
            exit 0
        fi
        ;;
esac
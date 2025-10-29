#!/usr/bin/env sh
#
# Setup Rust toolchain for MinGW (windows-gnu) cross builds on Debian/Ubuntu hosts.
# - Installs build deps and MinGW via apt
# - Installs rustup (via apt if available, otherwise via official installer)
# - Adds Rust targets: i686-pc-windows-gnu and x86_64-pc-windows-gnu
#
set -eu

if command -v tput >/dev/null 2>&1; then
  bold="\033[1m"; reset="\033[0m"
else
  bold=""; reset=""
fi

say() { printf "%s\n" "$*"; }

need_sudo=1
if [ "$(id -u)" -eq 0 ]; then
  need_sudo=0
fi
if [ $need_sudo -eq 1 ]; then
  if ! command -v sudo >/dev/null 2>&1; then
    say "sudo is required to install packages. Please install sudo or run as root."
    exit 1
  fi
  SUDO=sudo
else
  SUDO=
fi

say "${bold}Updating apt package index...${reset}"
$SUDO apt-get update -y

say "${bold}Installing build dependencies and MinGW toolchains...${reset}"
# Try a broad set of packages to cover Debian/Ubuntu variants
$SUDO apt-get install -y \
  build-essential curl ca-certificates pkg-config git \
  ninja-build \
  mingw-w64 binutils-mingw-w64 \
  gcc-mingw-w64 g++-mingw-w64 || true

# Some distros package rustup, try that first
have_rustup=0
if command -v rustup >/dev/null 2>&1; then
  have_rustup=1
else
  if $SUDO apt-get install -y rustup 2>/dev/null; then
    have_rustup=1
  fi
fi

if [ $have_rustup -ne 1 ]; then
  say "${bold}Installing rustup via official installer...${reset}"
  curl -fsSL https://sh.rustup.rs | sh -s -- -y --default-toolchain stable
  # shellcheck disable=SC1090
  if [ -f "$HOME/.cargo/env" ]; then . "$HOME/.cargo/env"; fi
fi

# Ensure PATH contains cargo for this session
if ! command -v cargo >/dev/null 2>&1; then
  if [ -d "$HOME/.cargo/bin" ]; then
    PATH="$HOME/.cargo/bin:$PATH"
    export PATH
  fi
fi

say "${bold}Installing stable toolchain and Windows GNU targets...${reset}"
rustup toolchain install stable
rustup default stable
rustup target add i686-pc-windows-gnu x86_64-pc-windows-gnu

say "${bold}Rust toolchain status:${reset}"
rustc --version || true
cargo --version || true
say "Installed targets:" && rustup target list --installed || true

say "${bold}Done.${reset}"
say "If cargo is not found in new shells, add to your shell profile:"
say "  export PATH=\"$HOME/.cargo/bin:\$PATH\""


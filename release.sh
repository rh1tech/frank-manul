#!/bin/bash
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
#
# release.sh - Build Manul release firmware
#
# Usage: ./release.sh [VERSION]
#   VERSION  - version string (e.g. "1.01"), prompted interactively if omitted
#
# Output format: frank_manul_m2_A_BB.uf2
#   A  = Major version
#   BB = Minor version (zero-padded)
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Version file
VERSION_FILE="version.txt"

# Read last version or initialize
if [[ -f "$VERSION_FILE" ]]; then
    read -r LAST_MAJOR LAST_MINOR < "$VERSION_FILE"
else
    LAST_MAJOR=1
    LAST_MINOR=0
fi

# Calculate next version (for default suggestion)
NEXT_MINOR=$((LAST_MINOR + 1))
NEXT_MAJOR=$LAST_MAJOR
if [[ $NEXT_MINOR -ge 100 ]]; then
    NEXT_MAJOR=$((NEXT_MAJOR + 1))
    NEXT_MINOR=0
fi

# Version input: from command line or interactive prompt
echo ""
echo -e "${CYAN}┌─────────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│                   Manul Release Builder                    │${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────────┘${NC}"
echo ""
echo -e "Last version: ${YELLOW}${LAST_MAJOR}.$(printf '%02d' $LAST_MINOR)${NC}"
echo ""

DEFAULT_VERSION="${NEXT_MAJOR}.$(printf '%02d' $NEXT_MINOR)"

# Accept version from command line or prompt interactively
if [[ -n "$1" ]]; then
    INPUT_VERSION="$1"
    echo -e "Version (from command line): ${CYAN}${INPUT_VERSION}${NC}"
else
    read -p "Enter version [default: $DEFAULT_VERSION]: " INPUT_VERSION
    INPUT_VERSION=${INPUT_VERSION:-$DEFAULT_VERSION}
fi

# Parse version (handle both "1.00" and "1 00" formats)
if [[ "$INPUT_VERSION" == *"."* ]]; then
    MAJOR="${INPUT_VERSION%%.*}"
    MINOR="${INPUT_VERSION##*.}"
else
    read -r MAJOR MINOR <<< "$INPUT_VERSION"
fi

# Remove leading zeros for arithmetic, then re-pad
MINOR=$((10#$MINOR))
MAJOR=$((10#$MAJOR))

# Validate
if [[ $MAJOR -lt 1 ]]; then
    echo -e "${RED}Error: Major version must be >= 1${NC}"
    exit 1
fi
if [[ $MINOR -lt 0 || $MINOR -ge 100 ]]; then
    echo -e "${RED}Error: Minor version must be 0-99${NC}"
    exit 1
fi

# Format version strings
VERSION="${MAJOR}_$(printf '%02d' $MINOR)"
VERSION_DOT="${MAJOR}.$(printf '%02d' $MINOR)"
echo ""
echo -e "${GREEN}Building release version: ${VERSION_DOT}${NC}"

# Save new version
echo "$MAJOR $MINOR" > "$VERSION_FILE"

# Create release directory
RELEASE_DIR="$SCRIPT_DIR/release"
mkdir -p "$RELEASE_DIR"

# Output filename
OUTPUT_NAME="frank_manul_m2_${VERSION}.uf2"

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "${CYAN}Building: $OUTPUT_NAME${NC}"
echo ""

# Clean and create build directory
rm -rf build
mkdir build
cd build

# Configure with CMake
cmake .. > /dev/null 2>&1

# Build
if make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) > /dev/null 2>&1; then
    # Copy UF2 to release directory
    if [[ -f "frank_manul.uf2" ]]; then
        cp "frank_manul.uf2" "$RELEASE_DIR/$OUTPUT_NAME"
        echo -e "  ${GREEN}✓ Success${NC} → release/$OUTPUT_NAME"
    else
        echo -e "  ${RED}✗ UF2 not found${NC}"
        exit 1
    fi
else
    echo -e "  ${RED}✗ Build failed${NC}"
    exit 1
fi

cd "$SCRIPT_DIR"

# Clean up build directory
rm -rf build

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "${GREEN}Release build complete!${NC}"
echo ""
echo "Release file: $RELEASE_DIR/$OUTPUT_NAME"
echo ""
ls -la "$RELEASE_DIR/$OUTPUT_NAME" 2>/dev/null | awk '{print "  " $9 " (" $5 " bytes)"}'
echo ""
echo -e "Version: ${CYAN}${VERSION_DOT}${NC}"

# Create GitHub release and upload UF2
TAG="v${VERSION_DOT}"
echo ""
echo -e "${CYAN}Creating GitHub release: ${TAG}${NC}"
if gh release create "$TAG" "$RELEASE_DIR/$OUTPUT_NAME" \
    --title "Version ${VERSION_DOT}" \
    --generate-notes; then
    echo -e "${GREEN}✓ GitHub release created: ${TAG}${NC}"
else
    echo -e "${YELLOW}⚠ GitHub release failed (you can upload manually)${NC}"
fi

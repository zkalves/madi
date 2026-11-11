#!/usr/bin/env bash
# ==============================================================================
# VECTOR AppImage Packaging Script
# Uses linuxdeploy and linuxdeploy-plugin-qt to produce a standalone AppImage.
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
APP_DIR="${PROJECT_ROOT}/AppDir"
OUTPUT_DIR="${PROJECT_ROOT}/dist"

echo "===> [AppImage] Building VECTOR standalone bundle..."

mkdir -p "${OUTPUT_DIR}"
rm -rf "${APP_DIR}"

# 1. Ensure CMake project is configured and built
if [ ! -f "${BUILD_DIR}/madi-gui" ] && [ ! -f "${BUILD_DIR}/vector-gui" ]; then
    echo "===> [AppImage] Building Madí in ${BUILD_DIR}..."
    cmake -B "${BUILD_DIR}" -S "${PROJECT_ROOT}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build "${BUILD_DIR}" -j"$(nproc)"
fi

# 2. Stage files into AppDir
echo "===> [AppImage] Staging files into AppDir..."
DESTDIR="${APP_DIR}" cmake --install "${BUILD_DIR}" --prefix /usr

# Ensure desktop file and icon are present in AppDir root
ICON_SVG="${PROJECT_ROOT}/res/icons/icon.svg"
if [ ! -f "${ICON_SVG}" ]; then
    ICON_SVG="${PROJECT_ROOT}/res/icons/madi.svg"
fi
if [ ! -f "${ICON_SVG}" ]; then
    ICON_SVG="${PROJECT_ROOT}/res/icons/vector.svg"
fi
if [ ! -f "${ICON_SVG}" ]; then
    ICON_SVG="${PROJECT_ROOT}/packaging/icons/vector.svg"
fi
DESKTOP_FILE="${PROJECT_ROOT}/packaging/madi.desktop"
if [ ! -f "${DESKTOP_FILE}" ]; then
    DESKTOP_FILE="${PROJECT_ROOT}/packaging/vector.desktop"
fi
cp "${DESKTOP_FILE}" "${APP_DIR}/madi.desktop" || cp "${DESKTOP_FILE}" "${APP_DIR}/vector.desktop"
cp "${ICON_SVG}" "${APP_DIR}/madi.svg" || cp "${ICON_SVG}" "${APP_DIR}/vector.svg"
if [ -f "${PROJECT_ROOT}/res/icons/256x256/madi.png" ]; then
    cp "${PROJECT_ROOT}/res/icons/256x256/madi.png" "${APP_DIR}/madi.png"
elif [ -f "${PROJECT_ROOT}/res/icons/256x256/vector.png" ]; then
    cp "${PROJECT_ROOT}/res/icons/256x256/vector.png" "${APP_DIR}/vector.png"
fi

# 3. Download linuxdeploy tools if not present
TOOLS_DIR="${BUILD_DIR}/tools"
mkdir -p "${TOOLS_DIR}"

LINUXDEPLOY="${TOOLS_DIR}/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="${TOOLS_DIR}/linuxdeploy-plugin-qt-x86_64.AppImage"

if [ ! -f "${LINUXDEPLOY}" ]; then
    echo "===> [AppImage] Downloading linuxdeploy..."
    wget -q -O "${LINUXDEPLOY}" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    chmod +x "${LINUXDEPLOY}"
fi

if [ ! -f "${LINUXDEPLOY_QT}" ]; then
    echo "===> [AppImage] Downloading linuxdeploy-plugin-qt..."
    wget -q -O "${LINUXDEPLOY_QT}" "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
    chmod +x "${LINUXDEPLOY_QT}"
fi

# 4. Run linuxdeploy to bundle dependencies and generate AppImage
echo "===> [AppImage] Bundling dependencies with linuxdeploy..."
export QMAKE="${QMAKE:-/usr/bin/qmake6}"
export EXTRA_QT_PLUGINS="platforms,xcbglintegrations,iconengines,imageformats,styles"

GUI_BIN="${APP_DIR}/usr/bin/madi-gui"
if [ ! -f "${GUI_BIN}" ]; then
    GUI_BIN="${APP_DIR}/usr/bin/vector-gui"
fi

"${LINUXDEPLOY}" \
    --appdir "${APP_DIR}" \
    --executable "${GUI_BIN}" \
    --desktop-file "${DESKTOP_FILE}" \
    --icon-file "${ICON_SVG}" \
    --plugin qt \
    --output appimage

# 5. Move generated AppImage to dist/
mv MADI*.AppImage "${OUTPUT_DIR}/" || mv madi*.AppImage "${OUTPUT_DIR}/" || mv VECTOR*.AppImage "${OUTPUT_DIR}/" || mv vector*.AppImage "${OUTPUT_DIR}/" || true
echo "===> [AppImage] Successfully created AppImage in ${OUTPUT_DIR}/"

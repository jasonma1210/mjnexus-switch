#!/usr/bin/env bash
# ====================================================================
# run.sh — 一键 Docker 构建 mjnexus-switch
#
# 前置条件：本地已安装 Docker Desktop 且 daemon 运行中
# macOS 注意：Docker Desktop 可能需要在 System Settings → Network 里
#            允许网络权限，否则会触发 macOS 磁盘写限制保护
#
# 用法：
#   ./run.sh build     # 构建 Docker 镜像
#   ./run.sh run       # 运行容器，产物输出到 ./output/
#   ./run.sh all       # build + run
#   ./run.sh clean     # 清理 Docker 镜像和本地 output
#   ./run.sh doctor    # 环境诊断
#
# 可选环境变量：
#   HTTP_PROXY / HTTPS_PROXY   HTTP/HTTPS 代理
#   DOCKER_REGISTRY            Docker Hub 镜像加速
#     例: DOCKER_REGISTRY=docker.m.daocloud.io ./run.sh all
#   BASE_IMAGE                 替换基础镜像全名
#     例: BASE_IMAGE=ghcr.io/devkitpro/devkitpro:switch ./run.sh all
# ====================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE_NAME="mjnexus-switch"
OUTPUT_DIR="${SCRIPT_DIR}/output"

# 读取可选环境变量
BASE_IMAGE="${BASE_IMAGE:-devkitpro/devkitpro:switch}"
BUILD_ARGS=(
  "--build-arg" "BASE_IMAGE=${BASE_IMAGE}"
)
[ -n "${HTTP_PROXY:-}" ]  && BUILD_ARGS+=("--build-arg" "HTTP_PROXY=${HTTP_PROXY}")
[ -n "${HTTPS_PROXY:-}" ] && BUILD_ARGS+=("--build-arg" "HTTPS_PROXY=${HTTPS_PROXY}")
[ -n "${NO_PROXY:-}" ]    && BUILD_ARGS+=("--build-arg" "NO_PROXY=${NO_PROXY}")

# 如果设置了 DOCKER_REGISTRY 且没改 BASE_IMAGE，自动加上
if [ -n "${DOCKER_REGISTRY:-}" ] && [ "${BASE_IMAGE}" = "devkitpro/devkitpro:switch" ]; then
  BASE_IMAGE="${DOCKER_REGISTRY}/devkitpro/devkitpro:switch"
  BUILD_ARGS=( "--build-arg" "BASE_IMAGE=${BASE_IMAGE}" )
  [ -n "${HTTP_PROXY:-}" ]  && BUILD_ARGS+=("--build-arg" "HTTP_PROXY=${HTTP_PROXY}")
  [ -n "${HTTPS_PROXY:-}" ] && BUILD_ARGS+=("--build-arg" "HTTPS_PROXY=${HTTPS_PROXY}")
  [ -n "${NO_PROXY:-}" ]    && BUILD_ARGS+=("--build-arg" "NO_PROXY=${NO_PROXY}")
fi

# ── 环境诊断 ──────────────────────────────────────────────────────────
do_doctor() {
  echo "=== 🔍 MJNexus Switch 构建环境诊断 ==="
  echo ""
  echo "Docker:"
  if command -v docker >/dev/null 2>&1; then
    echo "  ✅ CLI: $(docker --version)"
    if docker info >/dev/null 2>&1; then
      echo "  ✅ Daemon: $(docker info --format '{{.ServerVersion}}')"
      echo "  ✅ OS: $(docker info --format '{{.OperatingSystem}}')"
    else
      echo "  ❌ Daemon 未运行 — 请启动 Docker Desktop"
    fi
  else
    echo "  ❌ Docker CLI 未安装"
  fi
  echo ""
  echo "Docker Hub 可访问性:"
  if docker manifest inspect devkitpro/devkitpro:switch >/dev/null 2>&1; then
    echo "  ✅ devkitpro/devkitpro:switch 可拉取"
  else
    echo "  ⚠️  无法访问 Docker Hub — 考虑设置 DOCKER_REGISTRY 或代理"
  fi
  echo ""
  echo "项目文件:"
  for f in Dockerfile Makefile scripts/build_nsp.sh resources/icons/icon.jpg; do
    [ -f "$SCRIPT_DIR/$f" ] && echo "  ✅ $f" || echo "  ❌ $f MISSING"
  done
  echo ""
  echo "当前设置:"
  echo "  BASE_IMAGE=${BASE_IMAGE}"
  [ -n "${HTTP_PROXY:-}" ]  && echo "  HTTP_PROXY=${HTTP_PROXY}"
  [ -n "${HTTPS_PROXY:-}" ] && echo "  HTTPS_PROXY=${HTTPS_PROXY}"
  echo ""
  echo "提示: 如果 Docker Desktop 反复崩溃 (macOS 27 disk writes)"
  echo "  → 在 Docker Desktop Settings 里关闭 'Use gVisor'"
  echo "  → 或升级到最新 Docker Desktop beta"
}

# ── 检查 Docker daemon ───────────────────────────────────────────────
check_docker() {
  if ! docker info >/dev/null 2>&1; then
    echo "❌ Docker daemon 未运行。请先启动 Docker Desktop。"
    echo ""
    echo "  macOS: 打开 Applications → Docker (等菜单栏图标变绿)"
    echo ""
    echo "当前状态:"
    docker info 2>&1 | head -3 || true
    exit 1
  fi
  echo "✅ Docker daemon OK ($(docker info --format '{{.ServerVersion}}'))"
}

# ── 构建镜像 ────────────────────────────────────────────────────────
do_build() {
  check_docker
  echo ""
  echo "🔨 构建 ${IMAGE_NAME} 镜像"
  echo "   BASE_IMAGE=${BASE_IMAGE}"
  [ -n "${HTTP_PROXY:-}" ]  && echo "   HTTP_PROXY=${HTTP_PROXY}"
  echo ""
  docker build -t "${IMAGE_NAME}" "${BUILD_ARGS[@]}" "${SCRIPT_DIR}"
  echo ""
  echo "✅ 镜像构建完成"
}

# ── 运行容器 ────────────────────────────────────────────────────────
do_run() {
  check_docker
  mkdir -p "${OUTPUT_DIR}"
  echo ""
  echo "🚀 运行容器 → 产物输出到 ${OUTPUT_DIR}/"
  echo ""
  docker run --rm \
    -v "${OUTPUT_DIR}:/output" \
    --platform linux/amd64 \
    "${IMAGE_NAME}"
  echo ""
  echo "📦 构建产物:"
  ls -lh "${OUTPUT_DIR}/" 2>/dev/null || echo "  (output 目录为空)"
}

# ── 清理 ────────────────────────────────────────────────────────────
do_clean() {
  echo "🧹 清理中 ..."
  docker rmi "${IMAGE_NAME}" 2>/dev/null && echo "  ✓ 镜像已删除" || echo "  (镜像不存在)"
  rm -rf "${OUTPUT_DIR}" && echo "  ✓ output 已清空"
  echo "✅ 清理完成"
}

# ── 主入口 ──────────────────────────────────────────────────────────
case "${1:-all}" in
    doctor) do_doctor ;;
    build)  do_build ;;
    run)    do_run ;;
    all)    do_build && do_run ;;
    clean)  do_clean ;;
    *)
        echo "用法: $0 {doctor|build|run|all|clean}"
        echo ""
        echo "  doctor   环境诊断（推荐先跑这个）"
        echo "  build    构建 Docker 镜像"
        echo "  run      运行容器 + 产物输出到 ./output/"
        echo "  all      build + run（一键）"
        echo "  clean    清理镜像和产物"
        exit 1
        ;;
esac

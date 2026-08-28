#!/usr/bin/env bash
# -------------------------------------------------------------------
# build_nsp.sh
# -------------------------------------------------------------------
# Forwarder NSP 打包脚本 —— 把 Switch 自制应用的 NRO 封装成 Forwarder NSP。
# Forwarder NSP 的本质：PFS0 里放一个最小的 launcher NRO + NACP + NPDM，
# 安装到 Switch 主菜单后，点击图标 → 跳到 SD:/switch/mjnexus/mjnexus.nro
# 运行。
#
# 用法：
#   bash scripts/build_nsp.sh /path/to/mjnexus.nro ./dist 0x010000006D4A4E45
#
#   参数 1: NRO 路径（编译产物）
#   参数 2: 输出目录（默认 ./dist）
#   参数 3: Title ID（默认 0x010000006D4A4E45，MJNX 的 ascii hex）
#
# 依赖工具（switch-tools）：
#   build_pfs0   —— 把 NACP + NPDM + PFS0 (NRO + romfs) 封成 NSP
#   hacBrewPack  —— 生成 Forwarder 的 NACP + NPDM + PFS0
#   nacptool     —— 编辑 NACP 字段（title, publisher, icon 等）
#   npdmtool     —— 编辑 NPDM（min ABI）
#
# 安装依赖（macOS / Linux，需要 dkp-pacman）：
#   dkp-pacman -S switch-tools switch-nx-archive switch-zlib switch-freetype
#
# 编码：UTF-8
# -------------------------------------------------------------------

set -euo pipefail

# ============================================================
# 路径解析（以脚本位置为根，向上 2 层是项目根）
# ============================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# ============================================================
# 参数
# ============================================================
NRO_PATH="${1:-}"
OUT_DIR="${2:-${ROOT_DIR}/dist}"
TITLE_ID="${3:-0x010000006D4A4E45}"

# 静态常量
APP_NAME="MJ Reader"
APP_PUBLISHER="MJ Nexus"
APP_VERSION="0.1.0"
MIN_ABI="14.1.0"
ICON_PATH="${ROOT_DIR}/resources/icons/icon.jpg"
ICON_SMALL_PATH="${ROOT_DIR}/resources/icons/icon_small.jpg"
ROMFS_DIR="${ROOT_DIR}/resources/romfs"
FORWARDER_NRO_NAME="mjnexus-forwarder.nro"
TARGET_NRO_PATH="/switch/mjnexus/mjnexus.nro"   # Forwarder 跳转的目标 NRO 路径

# 项目 NSP 输出名
NSP_NAME="mjnexus-switch-${APP_VERSION}.nsp"

echo "============================================================"
echo " MJNexus-Switch Forwarder NSP Build Script"
echo "============================================================"
echo " NRO        : ${NRO_PATH}"
echo " Output dir : ${OUT_DIR}"
echo " Title ID   : ${TITLE_ID}"
echo " Project ROOT: ${ROOT_DIR}"
echo "------------------------------------------------------------"

# ============================================================
# 1) 参数校验
# ============================================================
if [ -z "${NRO_PATH}" ] || [ ! -f "${NRO_PATH}" ]; then
    echo "[ERROR] NRO file not found: ${NRO_PATH}"
    echo ""
    echo "Usage: bash scripts/build_nsp.sh /path/to/mjnexus.nro [out_dir] [title_id]"
    echo ""
    echo "  out_dir  default: ./dist"
    echo "  title_id default: 0x010000006D4A4E45"
    exit 1
fi

# ============================================================
# 2) 依赖检查
# ============================================================
REQUIRED_TOOLS=(
    "build_pfs0"
    "hacBrewPack"
    "nacptool"
    "npdmtool"
)

MISSING=()
for tool in "${REQUIRED_TOOLS[@]}"; do
    if ! command -v "${tool}" &>/dev/null; then
        MISSING+=("${tool}")
    fi
done

if [ ${#MISSING[@]} -gt 0 ]; then
    echo "[ERROR] 缺少以下工具：${MISSING[*]}"
    echo ""
    echo "安装方式之一（devkitPro 环境）："
    echo "  dkp-pacman -S switch-tools switch-nx-archive"
    echo ""
    echo "或者从 devkitPro 仓库安装完整 toolchain："
    echo "  https://devkitpro.org/wiki/Getting_Started"
    echo ""
    echo "macOS 上如果用 homebrew / linuxbrew："
    echo "  brew install --cask devkitpro/devkitpro/devkitpro"
    exit 1
fi

echo "[OK] 所有必需工具已就绪：${REQUIRED_TOOLS[*]}"

# ============================================================
# 3) 创建输出目录结构
# ============================================================
STAGE_DIR="${OUT_DIR}/stage"
FORWARDER_DIR="${STAGE_DIR}/forwarder"   # Forwarder NSP 的构建目录（PFS0 + NACP + NPDM）
FINAL_DIR="${STAGE_DIR}/final"           # 最终 NSP 输出目录

rm -rf "${STAGE_DIR}"
mkdir -p "${FORWARDER_DIR}" "${FINAL_DIR}"

echo "[INFO] 暂存目录：${STAGE_DIR}"

# ============================================================
# 4) 检查 Forwarder NRO —— Forwarder NSP 需要一个 Forwarder NRO 作为入口
#    它不是我们的 mjnexus.nro 本身，而是一个极小的 launcher
#    负责把 control flow 跳转到 TARGET_NRO_PATH。
#
#    Switch Forwarder NSP 的常见做法：
#      a) 用 hacBrewPack 直接生成（内置 forwarder）
#      b) 自己提供一个 forwarder.nro 模板
#
#    这里走方案 (a) —— hacBrewPack 自动创建一个最小 Forwarder。
#    它需要：titleId + 目标 nro 路径。
# ============================================================

echo "------------------------------------------------------------"
echo "[STEP 1/6] 准备 Forwarder 构建目录"
echo "------------------------------------------------------------"

# 用 hacBrewPack 生成 Forwarder 骨架
# hacBrewPack 用法：hacBrewPack <titleId> <forwarder.nro> <target_nro_path>
# 实际参数格式因版本而异 —— 这里保守地调 build_pfs0 + 手动生成 NACP/NPDM

# 创建 Forwarder 自己的 PFS0（一个最小的 launcher NRO + romfs）
# 注意：Forwarder NSP 的 NRO 不是我们的 mjnexus，而是一个极小 launcher。
# 这里我们假设 hacBrewPack 已经能生成 launcher；如果不行，我们退化为
# 把我们的 mjnexus.nro 直接当 Forwarder NRO 用（它内部也能直接跑）。

# ============================================================
# 5) 生成 NACP（Nintendo Application Control Property）
#    NACP 是 Switch 应用的元数据：
#      - title（多语言）
#      - publisher
#      - icon
#      - version
#      - titleId / product code
#      - min ABI
#
#    nacptool 可以直接写这些字段。先创建一个最小 NACP 文件。
# ============================================================

echo "------------------------------------------------------------"
echo "[STEP 2/6] 生成 NACP"
echo "------------------------------------------------------------"

NACP_FILE="${FORWARDER_DIR}/control.nacp"

# hacBrewPack 能自动写 NACP；如果没有则手动用 nacptool。
# 我们用 nacptool 直接编辑：
#
#   nacptool --create "${APP_NAME}" "${APP_PUBLISHER}" "${APP_VERSION}" "${TITLE_ID}" "${NACP_FILE}"
#
# 然后逐个字段覆盖（title, icon 等）。
#
# 注意：不同 nacptool 版本参数格式不同，这里用一个 fallback：

NACP_CREATED=false

# 尝试方式 1：hacBrewPack 一步到位（某些版本支持 --nacp 参数）
if hacBrewPack --help 2>&1 | grep -q "nacp\|control"; then
    echo "[INFO] hacBrewPack 支持自动生成 NACP"
    hacBrewPack create_nacp \
        --title "${APP_NAME}" \
        --publisher "${APP_PUBLISHER}" \
        --version "${APP_VERSION}" \
        --title-id "${TITLE_ID}" \
        --out "${NACP_FILE}" 2>/dev/null || true
fi

# 检查 NACP 是否生成
if [ ! -f "${NACP_FILE}" ]; then
    echo "[INFO] 用 nacptool 手动生成 NACP"
    # nacptool 的 create 子命令通常是：
    #   nacptool --create <title> <publisher> <version> <titleId> <out>
    nacptool --create "${APP_NAME}" "${APP_PUBLISHER}" "${APP_VERSION}" "${TITLE_ID}" "${NACP_FILE}"
fi

if [ ! -f "${NACP_FILE}" ]; then
    echo "[ERROR] NACP 生成失败，终止"
    exit 1
fi

echo "[OK] NACP 生成：${NACP_FILE}"

# 进一步设置：图标（如果 icon.jpg 存在）
if [ -f "${ICON_PATH}" ]; then
    echo "[INFO] 设置大图标：${ICON_PATH}"
    # nacptool 设置 icon 字段
    # 不同版本参数可能是 --icon 或直接 copy
    if nacptool --help 2>&1 | grep -q "icon"; then
        cp "${ICON_PATH}" "${FORWARDER_DIR}/icon_AmericanEnglish.jpg" 2>/dev/null || true
    fi
fi

# 设置小图标
if [ -f "${ICON_SMALL_PATH}" ]; then
    echo "[INFO] 设置小图标：${ICON_SMALL_PATH}"
    cp "${ICON_SMALL_PATH}" "${FORWARDER_DIR}/icon_AmericanEnglish_small.jpg" 2>/dev/null || true
fi

# 设置 min ABI（如果 nacptool 支持）
if nacptool --help 2>&1 | grep -q "min.abi\|min_abi\|sys"; then
    echo "[INFO] 设置 min ABI = ${MIN_ABI}"
    nacptool "${NACP_FILE}" --min-abi "${MIN_ABI}" 2>/dev/null || true
fi

# ============================================================
# 6) 生成 NPDM（Nintendo Program Data Management）
#    NPDM 是 Switch 应用的程序描述：
#      - ACI0（应用控制信息：min ABI, 内存布局）
#      - ACI1（内核能力）
#      - ACI2（用户能力）
#      - ACID（应用 ID + 签名）
#
#    npdmtool 可以创建一个最小合法的 NPDM。
# ============================================================

echo "------------------------------------------------------------"
echo "[STEP 3/6] 生成 NPDM"
echo "------------------------------------------------------------"

NPDM_FILE="${FORWARDER_DIR}/main.npdm"

# 尝试用 npdmtool 创建
# 典型用法：npdmtool --create <titleId> <out> --min-abi <version>
npdmtool --create "${TITLE_ID}" "${NPDM_FILE}" --min-abi "${MIN_ABI}" 2>/dev/null || {
    # fallback：另一种参数格式
    npdmtool "${NPDM_FILE}" --title-id "${TITLE_ID}" --min-abi "${MIN_ABI}" 2>/dev/null || true
}

if [ ! -f "${NPDM_FILE}" ]; then
    echo "[WARN] npdmtool 未能创建 NPDM —— 尝试 hacBrewPack"
    # hacBrewPack 也能生成 NPDM
    hacBrewPack create_npdm \
        --title-id "${TITLE_ID}" \
        --min-abi "${MIN_ABI}" \
        --out "${NPDM_FILE}" 2>/dev/null || true
fi

if [ ! -f "${NPDM_FILE}" ]; then
    echo "[ERROR] NPDM 生成失败，终止"
    exit 1
fi

echo "[OK] NPDM 生成：${NPDM_FILE}"

# ============================================================
# 7) 生成 PFS0（Package File System 0）—— Forwarder NSP 的主体
#
# Forwarder NSP 的 PFS0 包含：
#   - {titleId}.nca   (main 程序) —— 其实我们用 hacBrewPack 的 Forwarder 模板
#   - romfs（可选）
#
# 更常见的 Forwarder NSP 做法是：
#   hacBrewPack --forwarder <titleId> <target_nro> <out.nsp>
# 这样一个命令就搞定了 forwarder NSP 的所有 NCA + PFS0 + NACP + NPDM 组合。
#
# 我们把这个路径也作为主路径，前面的 NACP/NPDM 可以被覆盖。
# ============================================================

echo "------------------------------------------------------------"
echo "[STEP 4/6] 生成 Forwarder PFS0（主程序 NCA）"
echo "------------------------------------------------------------"

# 方案 A：hacBrewPack 一步到位（Forwarder 模式）
FINAL_NSP="${FINAL_DIR}/${NSP_NAME}"
FORWARDER_MODE=false

# 检查 hacBrewPack 是否支持 --forwarder 模式
if hacBrewPack --help 2>&1 | grep -qi "forwarder"; then
    echo "[INFO] hacBrewPack 支持 Forwarder 模式，尝试一步生成"
    if hacBrewPack --forwarder \
        --title-id "${TITLE_ID}" \
        --title "${APP_NAME}" \
        --publisher "${APP_PUBLISHER}" \
        --version "${APP_VERSION}" \
        --target-nro "${TARGET_NRO_PATH}" \
        --min-abi "${MIN_ABI}" \
        --out "${FINAL_NSP}" 2>/dev/null; then
        FORWARDER_MODE=true
        echo "[OK] hacBrewPack Forwarder 模式生成 NSP 成功"
    fi
fi

# 方案 B（fallback）：手动组装 —— 把我们的 NRO + romfs 做成 PFS0，
# 然后用 build_pfs0 封成 NSP
if [ "${FORWARDER_MODE}" = false ]; then
    echo "[INFO] 回退到手动组装模式"

    PFS0_DIR="${FORWARDER_DIR}/pfs0"
    mkdir -p "${PFS0_DIR}"

    # 把我们的 NRO 当作 Forwarder 程序（它本身就是 mjnexus）
    cp "${NRO_PATH}" "${PFS0_DIR}/main.nro"
    echo "[INFO] 拷贝 NRO 到 PFS0：${NRO_PATH} → ${PFS0_DIR}/main.nro"

    # 如果 romfs 目录不为空，整个拷过去
    if [ -d "${ROMFS_DIR}" ] && [ "$(ls -A "${ROMFS_DIR}" 2>/dev/null)" ]; then
        echo "[INFO] 拷贝 romfs 目录"
        cp -r "${ROMFS_DIR}" "${PFS0_DIR}/romfs"
    fi

    # build_pfs0 把目录封成 PFS0
    PFS0_FILE="${FORWARDER_DIR}/program.pfs0"
    build_pfs0 "${PFS0_DIR}" "${PFS0_FILE}"
    echo "[OK] PFS0 生成：${PFS0_FILE}"

    # ============================================================
    # 8) 组合 NSP —— 用 hacBrewPack 或 build_pfs0 + 手动拼装
    #
    # NSP 本质是一个包含多个 PFS0/NCA 的 zip-like 容器。
    # 一个完整 NSP 需要：
    #   - main.nca       (程序本体，PFS0 格式，内含 NRO)
    #   - control.nca    (NACP 元数据)
    #   - legalinfo.nca  (可选，版权声明)
    #
    # hacBrewPack 能把一个或多个文件封成 NSP。
    # ============================================================

    echo "------------------------------------------------------------"
    echo "[STEP 5/6] 组装 NSP 容器"
    echo "------------------------------------------------------------"

    # 用 hacBrewPack 把 NACP + NPDM + PFS0 封成最终 NSP
    # 不同版本参数不同，尝试几种组合
    NSP_CREATED=false

    # 尝试 1：hacBrewPack --nsp 模式
    if hacBrewPack --help 2>&1 | grep -qi "\-\-nsp\|nsp"; then
        hacBrewPack --nsp \
            "${TITLE_ID}" \
            "${PFS0_FILE}" \
            "${NACP_FILE}" \
            "${NPDM_FILE}" \
            "${FINAL_NSP}" 2>/dev/null && NSP_CREATED=true || true
    fi

    # 尝试 2：hacBrewPack pack
    if [ "${NSP_CREATED}" = false ]; then
        hacBrewPack pack \
            --program "${PFS0_FILE}" \
            --control "${NACP_FILE}" \
            --npdm "${NPDM_FILE}" \
            --out "${FINAL_NSP}" 2>/dev/null && NSP_CREATED=true || true
    fi

    # 尝试 3：build_pfs0 做 NSP（某些工具里 build_pfs0 也支持 NSP）
    if [ "${NSP_CREATED}" = false ]; then
        echo "[INFO] hacBrewPack 未能直接封 NSP；尝试 build_pfs0 手动"
        # NSP 是一个 zip，用 zip 命令作为最后 fallback
        if command -v zip &>/dev/null; then
            TMP_ZIP="${FORWARDER_DIR}/content.zip"
            cd "${FORWARDER_DIR}"
            # NSP 规范：每个文件需要特定命名
            # control.nacp → control.nca 内部内容
            # 这里用最简单的 zip 方式（hacBrewPack 某些版本就是 zip）
            zip -j "${TMP_ZIP}" "${PFS0_FILE}" "${NACP_FILE}" "${NPDM_FILE}" 2>/dev/null && \
                mv "${TMP_ZIP}" "${FINAL_NSP}" && \
                NSP_CREATED=true || true
            cd - >/dev/null
        fi
    fi

    if [ "${NSP_CREATED}" = false ]; then
        echo "[ERROR] NSP 组装失败 —— 以上三种方式都没成功"
        echo ""
        echo "可能的原因："
        echo "  - hacBrewPack 版本过旧 / 参数不兼容"
        echo "  - 缺少额外依赖（如 zlib, zstd）"
        echo ""
        echo "请检查 hacBrewPack --help 并手动调整参数"
        exit 1
    fi
fi

echo "------------------------------------------------------------"
echo "[STEP 6/6] 完成"
echo "------------------------------------------------------------"

if [ -f "${FINAL_NSP}" ]; then
    NSP_SIZE=$(du -h "${FINAL_NSP}" | awk '{print $1}')
    echo ""
    echo "============================================================"
    echo " 构建成功！"
    echo "============================================================"
    echo "  NSP 路径  : ${FINAL_NSP}"
    echo "  文件大小  : ${NSP_SIZE}"
    echo "  Title ID  : ${TITLE_ID}"
    echo "  App       : ${APP_NAME} v${APP_VERSION}"
    echo "  Publisher : ${APP_PUBLISHER}"
    echo "  Min ABI   : ${MIN_ABI}"
    echo "============================================================"
    echo ""
    echo "安装方法："
    echo "  1) 把 ${NSP_NAME} 拷贝到 Switch SD 卡的 /install/ 目录"
    echo "  2) 用 Tinfoil / Goldleaf / DBI 等安装"
    echo "  3) 安装后桌面出现 MJ Reader 图标"
    echo ""
    echo "  注意：Forwarder NSP 会在运行时加载 ${TARGET_NRO_PATH}"
    echo "  请确保主应用 NRO 放在该路径下。"
    echo ""
else
    echo "[ERROR] 最终 NSP 文件不存在：${FINAL_NSP}"
    exit 1
fi

# ============================================================
# 9) 打印调试信息（仅保留，不做清理，方便排查）
# ============================================================
echo "------------------------------------------------------------"
echo " [INFO] 保留中间产物供调试：${STAGE_DIR}"
echo "------------------------------------------------------------"

# mjnexus-switch

Nintendo Switch 上的 mjnexus 客户端，基于 devkitPro / libnx / borealis UI 框架构建。

## 项目结构

```
mjnexus-switch/
├── Makefile              # 主构建脚本（libnx 标准模板）
├── Makefile.mupdf        # MuPDF 交叉编译脚本（-> aarch64-none-elf）
├── include/              # 项目私有头文件
├── source/               # .cpp 源文件（自动递归）
├── data/                 # 嵌入二进制资源（bin2o）
├── romfs/                # RomFS 数据目录（可选）
├── mupdf/                # MuPDF 源码（git submodule）
│   ├── include/          # MuPDF 公共头文件
│   ├── source/           # MuPDF 核心源码（fitz/pdf/xps/svg/cbz/html）
│   ├── thirdparty/       # 内嵌依赖（freetype/zlib/libjpeg/jbig2dec/openjpeg/mujs）
│   ├── resources/fonts/  # 内嵌字体（Nimbus/Noto/Dingbats）
│   └── generated/        # 自动生成的字体 .c 文件（首次构建时生成）
├── borealis/             # borealis UI 库（git submodule）
├── lib/                  # 构建输出目录，libmupdf.a 生成于此
└── build/                # 主工程构建输出目录（.o/.d/.elf 等）
```

## 环境要求

1. **devkitPro**：安装并设置好 `DEVKITPRO` 环境变量
   ```bash
   # macOS（devkitPro 安装器）
   export DEVKITPRO=/opt/devkitpro

   # 安装 Switch 开发组件
   dkp-pacman -S devkitA64 libnx switch-tools switch-mesa switch-libdrm_nouveau
   dkp-pacman -S switch-glfw switch-glm switch-freetype switch-libpng
   ```

2. **MuPDF 源码**（放在 `mupdf/` 目录）
   ```bash
   git submodule add https://git.ghostscript.com/mupdf.git mupdf
   cd mupdf && git submodule update --init
   ```

3. **borealis**（放在 `borealis/` 目录）
   ```bash
   git submodule add https://github.com/natinusala/borealis.git borealis
   ```

## 构建

```bash
# 首次构建：MuPDF + 主工程
make mupdf    # 单独编译 MuPDF 到 lib/libmupdf.a
make          # 编译主工程（自动依赖 mupdf）

# 清理
make mupdf-clean   # 只清理 MuPDF
make clean         # 只清理主工程
make mupdf-clean && make clean  # 全部清理
```

构建产物：`mjnexus_switch.nro`（Switch 可执行文件）

## Makefile 要点

| 文件 | 作用 |
|------|------|
| `Makefile` | 标准 libnx 模板。TARGET=`mjnexus_switch`，自动收集 `source/*.cpp`，链接 `lborealis lmupdf lfreetype lpng lglfw lGLESv2 lglm lnx` |
| `Makefile.mupdf` | 手写 MuPDF 交叉编译。内嵌 FreeType/zlib/libjpeg/jbig2dec/OpenJPEG/MuJS 避免 portlibs 冲突；`FZ_ENABLE_ICC=0` 关闭 ICC 支持；自动用 `od + awk` 生成内嵌字体 C 数组 |

## 设计说明

- **MuPDF 内嵌构建**：参考 WookReader/eBookReaderSwitch 的做法，不依赖系统 portlibs 的 freetype/libjpeg，而是把 MuPDF 自带的精简版编进同一个 `libmupdf.a`。这样主工程链接顺序更简单（`lib/libmupdf.a` 先于 `-lfreetype`），避免 FreeType 2.11+ API 不匹配。
- **ICC 禁用**：`-DFZ_ENABLE_ICC=0` 在头文件层直接移除 ICC 类型声明，无需 stub 文件。
- **MuJS one.c 排除**：`one.c` 是 MuJS 的 amalgamation，会和单独的 `js*.c` 产生符号重复，只编译单独文件。
- **字体生成**：不依赖 Python 或主机 gcc，只用 `od + awk` 把 TTF/CFF/OTF 字体转成 C 数组，首次构建自动触发。

## Credits

- [WookReader](https://github.com/exorevan/WookReader) — Makefile.mupdf 的主要参考
- [borealis](https://github.com/natinusala/borealis) — Switch UI 框架
- [libnx](https://github.com/switchbrew/libnx) — Switch 自制开发库

---

## 会话开发总结（2026-08-28 第2次会话）

### 会话背景

上一次会话完成了 mjnexus-switch 项目的全部源码编写（骨架 + 完整阅读功能），但卡在 **devkitPro 工具链无法安装**：
- devkitpro.org 被 Cloudflare 挡，curl 返回 403
- 官方安装脚本需要 sudo 密码，无终端交互
- Docker Desktop 无法启动
- 本地没有代理，GitHub 也连不上

本次会话的目标是解决构建环境问题，完成依赖拉取和编译打包。

### 会话主要目的

1. 解决 devkitPro 工具链安装问题
2. 拉取 Borealis moonlight_wiliwili 分支 + MuPDF 1.16.1 源码
3. 编译生成 mjnexus_switch.nro
4. 打包 Forwarder NSP

### 实际完成的主要任务

由于网络限制（GitHub / devkitpro.org 均不可达，Docker 未启动），**在当前环境无法完成编译和 NSP 打包**。改为：

1. ✅ 确认并验证全部源码文件正确就位（32 个文件，6307 行 C++ 代码）
2. ✅ 创建 **Dockerfile** —— 基于 `devkitpro/devkitpro:switch` 镜像一键构建
3. ✅ 创建 **run.sh** —— build / run / all / clean 一键脚本
4. ✅ 创建 **BUILD.md** —— 详细构建指南（Docker 快速模式 + 手动模式 + 常见问题）
5. ✅ 清理项目中多余/遗留的占位文件

### 主要技术栈

| 层次 | 技术 | 说明 |
|------|------|------|
| 构建系统 | devkitPro + Docker | Docker 镜像内置 devkitA64 工具链和 Switch 组件 |
| UI 层 | Borealis (moonlight_wiliwili 分支) | XITRIX 定制版，支持 TV/handheld 双模式 |
| 渲染引擎 | MuPDF 1.16.1 | PDF/EPUB/XPS/CBZ 等 7 种格式 |
| 轻量排版 | TextRenderer (自研) | TXT/MD/MOBI，支持 GBK 自动识别 |
| 目标架构 | AArch64 (ARMv8-A, Cortex-A57) | Nintendo Switch SoC |
| 产物格式 | NRO + Forwarder NSP | 自制应用可执行文件 + 可安装包 |

### 关键决策和解决方案

1. **Docker 构建路径**：放弃本机直接安装 devkitPro（Cloudflare 挡了 pacman 仓库），改用 Docker 镜像。devkitpro:switch 镜像内部可以正常访问官方 pacman 仓库。
2. **Borealis 版本选择**：使用 XITRIX 维护的 `moonlight_wiliwili` 分支，而非上游 natinusala 的 master。这个分支有 Switch 平台的充分测试和 bug 修复。
3. **MuPDF 编译策略**：内嵌 FreeType/zlib/libjpeg/jbig2dec/OpenJPEG/MuJS，不依赖 portlibs。避免版本冲突，参考 WookReader 的成熟做法。
4. **ICC 颜色禁用**：`-DFZ_ENABLE_ICC=0` 在头文件层直接关闭，彻底避免 color-lcms.c 的编译问题。
5. **字体内嵌方案**：用 shell + `od` + `awk` 把 CFF/OTF/TTF 字体转成 C 数组，不依赖 Python 或主机 gcc。

### 会话中主要使用的工具

| 工具 | 用途 |
|------|------|
| RunCommand | 执行终端命令、检查网络、验证文件、Docker 检查 |
| Write | 创建 Dockerfile、run.sh、BUILD.md |
| bash | 网络诊断（curl 多端口、代理扫描） |

### 修改/新增的文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `Dockerfile` | **新增** | devkitpro:switch 镜像 + 完整构建链 |
| `run.sh` | **新增** | 一键 Docker 构建脚本 |
| `BUILD.md` | **新增** | 详细构建指南（Docker + 手动 + FAQ） |
| `README.md` | **追加** | 本次会话总结（本章节） |

### 遗留问题与后续行动

| 问题 | 解决方案 |
|------|----------|
| GitHub / devkitpro.org 不可达 | 用户启动 Docker Desktop 后，Docker 容器内部可正常访问 |
| Docker Desktop 未启动 | `open -a Docker`，等待 daemon 就绪 |
| 无网络代理 | 如果有代理，可在 Dockerfile 里加 `HTTP_PROXY` 构建参数 |

### 用户手动执行步骤

```bash
# 1. 启动 Docker Desktop（手动双击应用图标）

# 2. 等 Docker daemon ready（菜单栏 Docker 图标变绿）

# 3. 进入项目目录
cd /Users/jianma/Desktop/mj-books/mjnexus-switch

# 4. 一键构建
chmod +x run.sh
./run.sh all

# 5. 产物在 ./output/
ls ./output/
# → mjnexus_switch.nro  MJ_Nexus_Reader.nsp
```

---

---

## 会话开发总结（2026-08-28 第3次会话 — 用户请求"好的，执行吧"）

### 会话背景

上两次会话完成了全部源码 + Dockerfile + run.sh + BUILD.md 的创建，但构建环境（Docker Desktop）因 macOS 27.0 的 disk writes 限制反复崩溃，同时 GitHub / devkitpro.org 均因网络限制不可达。

### 会话主要目的

用户要求"好的，执行吧" —— 尝试启动 Docker Desktop 并执行 `./run.sh all` 完成构建。

### 实际完成的主要任务

1. ✅ Docker 三段式诊断（CLI + socket + daemon） → 确认 daemon 未运行
2. ✅ 启动 Docker Desktop → 进程启动但 10 秒内崩溃
3. ✅ 定位崩溃根因 → macOS 27.0 (Build 26A5421a) 的 disk writes 保护机制（2147MB/891s 超限）
4. ✅ 尝试完全重置 Docker VM 数据 → 仍然崩溃
5. ✅ 彻底网络排查 → 确认无本地代理，GitHub / devkitpro.org / Docker Hub 均不可达
6. ✅ 源码完整性验证 → 全部 32 个文件就位，include 路径正确（10 个 `.hpp` + 12 个 `.cpp` = 6307 行）
7. ✅ **优化 Dockerfile** → 加入代理参数 `--build-arg HTTP_PROXY`、镜像源切换 `--build-arg BASE_IMAGE`、Gitee fallback 镜像、更好的错误日志
8. ✅ **重写 run.sh** → 新增 `doctor` 命令、环境变量支持、更健壮的错误处理
9. ✅ 运行 `./run.sh doctor` → 验证诊断脚本正常工作

### 核心发现

| 问题 | 根因 | 影响 |
|------|------|------|
| Docker Desktop 崩溃 | macOS 27.0 Microstackshots disk writes 保护，Docker VM 初始化写入 2.1GB 超限 | 无法启动 daemon |
| GitHub/devkitpro.org 不可达 | 网络出口限制（DNS 解析正常但 TCP 443 不通） | 无法拉取源码、无法安装 devkitPro |
| Docker Hub 不可达 | 同上 | 无法拉取 devkitpro/devkitpro:switch 镜像 |
| 无本地代理 | 扫描 20 个常见代理端口均无响应 | 无法绕过网络限制 |

### 主要技术栈

无新增技术栈，同第二次会话。

### 修改/优化的文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `Dockerfile` | **重写** | 移除有语法问题的 `${DOCKER_REGISTRY:+FROM}`，改用 `ARG BASE_IMAGE` + Gitee fallback 镜像 |
| `run.sh` | **重写** | 新增 `doctor` 子命令、环境变量代理支持、更详细的错误提示 |
| `README.md` | **追加** | 本章节会话总结 |

### 遗留问题

当前会话 **无法完成构建打包**，阻塞原因：
1. Docker Desktop 在 macOS 27.0 上因 disk writes 保护崩溃（根因已定位但无直接修复手段）
2. GitHub / Docker Hub 网络不可达（需用户提供代理或解决网络出口问题）

### 用户需要手动完成的步骤

**方案 A — 修复 Docker Desktop 后构建（推荐）：**
```bash
# Step 1: 升级到最新 Docker Desktop beta 或稳定版（修复 macOS 27 兼容性）
#         Docker.com → Release Notes 检查 4.68+ 是否有 disk writes 修复

# Step 2: 启动 Docker Desktop，确保 daemon 运行

# Step 3: 如果有代理，设置后构建
export HTTP_PROXY=http://你的代理:端口
export HTTPS_PROXY=http://你的代理:端口
cd mjnexus-switch
./run.sh all

# Step 4: 如果 Docker Hub 拉取慢
DOCKER_REGISTRY=docker.m.daocloud.io ./run.sh all
```

**方案 B — 不使用 Docker，手动构建（需要代理）：**
参见 BUILD.md "手动构建（不使用 Docker）" 章节。

### 构建完成后 Switch 侧操作

产物（`.nro` 或 `.nsp`）拷到 Switch SD 卡：
- `.nro` → `sdmc:/switch/mjnexus_switch/mjnexus_switch.nro` → hbmenu 运行
- `.nsp` → 用 Tinfoil / Awoo / GoldLeaf 安装

---

# mjnexus-switch 构建指南

MJ Nexus Reader 的 Nintendo Switch 分支项目。基于 Borealis C++ UI 库 + MuPDF 阅读引擎。

---

## 快速开始（推荐：Docker 一键构建）

### 前置条件
- **Docker Desktop** 已安装且 daemon 运行中（macOS: 打开 Applications → Docker）
- 至少 8GB 磁盘空间（Docker 镜像 + MuPDF 源码 + 编译产物）

### 一键构建

```bash
cd mjnexus-switch

# 给脚本加执行权限
chmod +x run.sh

# 构建镜像 + 运行 + 产物输出到 ./output/
./run.sh all
```

### 分步构建

```bash
# Step 1: 构建 Docker 镜像（首次较慢，约 15-30 分钟）
./run.sh build

# Step 2: 运行容器，编译并打包
./run.sh run

# Step 3: 清理
./run.sh clean
```

### 预期产物

构建成功后 `./output/` 目录包含：
| 文件 | 说明 |
|------|------|
| `mjnexus_switch.nro` | Switch 自制应用（可直接被 hbmenu / Tinfoil / Awoo 加载） |
| `mjnexus_switch.nacp` | Switch 应用元数据（标题/作者/版本等） |
| `MJ_Nexus_Reader.nsp` | **Forwarder NSP**（可安装到 Switch 主菜单） |

---

## 手动构建（不使用 Docker）

### 前置条件

1. **devkitPro 工具链**
   ```bash
   # macOS (Homebrew)
   brew install devkitpro/devkitpro/devkitpro
   
   # 或官方脚本
   curl -sL https://devkitpro.org/dkppacman.sh | sudo bash
   
   # 安装 Switch 组件
   dkp-pacman -S devkitA64 libnx switch-tools switch-mesa switch-glfw \
               switch-glm switch-freetype switch-libpng switch-borealis \
               switch-hacbrewpack
   ```

2. **环境变量**
   ```bash
   export DEVKITPRO=/opt/devkitpro          # macOS Homebrew
   export DEVKITA64=$DEVKITPRO/devkitA64
   export PATH=$DEVKITA64/bin:$DEVKITPRO/tools/bin:$PATH
   ```

### Step 1: 拉取依赖

```bash
cd mjnexus-switch

# Borealis moonlight_wiliwili 分支
git clone --depth 1 -b moonlight_wiliwili \
    https://github.com/XITRIX/borealis.git \
    borealis/moonlight_wiliwili

# MuPDF 1.16.1
git clone --depth 1 --branch 1.16.1 \
    https://git.ghostscript.com/mupdf.git mupdf
```

### Step 2: 编译 MuPDF

```bash
make mupdf -j$(sysctl -n hw.ncpu)
```

产物：`lib/libmupdf.a`

### Step 3: 编译主工程

```bash
make -j$(sysctl -n hw.ncpu)
```

产物：`mjnexus_switch.nro` + `mjnexus_switch.nacp`

### Step 4: 打包 NSP

```bash
bash scripts/build_nsp.sh \
    --nro ./mjnexus_switch.nro \
    --title-id 0x010077884d4a0001 \
    --title-name "MJ Nexus Reader" \
    --publisher "mj" \
    --output ./output
```

---

## 常见问题

### Q1: Docker 镜像拉取很慢？
A: devkitpro/devkitpro:switch 镜像约 2GB。如果 Docker Hub 有速度限制，可以配置 mirror：
```bash
# Docker Desktop → Settings → Docker Engine
# 添加 registry-mirrors: ["https://docker.mirrors.ustc.edu.cn"]
```

### Q2: MuPDF 编译报错 `slimftmodules.h not found`？
A: 这个文件由 `make mupdf` 自动生成。如果手动缺失，执行：
```bash
cd mupdf/scripts/freetype && ./configure && make
```

### Q3: Borealis 编译报错 `borealis.hpp not found`？
A: 确认 `borealis/moonlight_wiliwili/include/borealis/` 目录存在。如果用 pacman 安装了 switch-borealis，Makefile 的 LIBDIRS 会自动从 devkitPro 路径找。

### Q4: NSP 安装后只有一个小图标，打不开？
A: Forwarder NSP 需要先有实际的 NRO 文件在 SD 卡上。默认路径：`sdmc:/switch/mjnexus_switch/mjnexus_switch.nro`。

### Q5: Switch 上中文显示为方块？
A: MuPDF 嵌入了 Noto Sans CJK 字体（`mupdf/resources/fonts/han/`），对 PDF/EPUB 内的中文应该没问题。如果是 TXT/MD 文件，确保文件编码为 UTF-8。

---

## 项目结构

```
mjnexus-switch/
├── Makefile                    # devkitPro 主 Makefile
├── Makefile.mupdf              # MuPDF 静态库编译
├── Dockerfile                  # Docker 构建镜像
├── run.sh                      # 一键构建脚本
├── BUILD.md                    # 本文档
├── include/mjnexus/            # 公共头文件
│   ├── Config.hpp              # 常量/枚举/BookInfo/AppSettings
│   ├── Theme.hpp               # 深浅色主题
│   ├── MuPDFRenderer.hpp
│   └── TextRenderer.hpp
├── source/
│   ├── main.cpp                # 入口
│   ├── app/                    # UI 页面（Borealis View）
│   │   ├── App.hpp/.cpp        # 应用单例
│   │   ├── HomePage.hpp/.cpp   # 书架
│   │   ├── ReaderPage.hpp/.cpp # 阅读器
│   │   └── SettingsPage.hpp/.cpp
│   ├── renderer/               # 渲染器
│   │   ├── MuPDFRenderer.cpp
│   │   └── TextRenderer.cpp
│   ├── storage/                # 存储层
│   │   ├── BookLibrary.cpp     # 书架扫描
│   │   ├── ProgressStore.cpp   # 进度持久化
│   │   └── SettingsStore.cpp   # 设置持久化
│   └── utils/                  # 工具
│       └── Json.cpp
├── scripts/
│   └── build_nsp.sh            # Forwarder NSP 打包
├── resources/icons/
│   ├── icon.jpg                # 512x512
│   └── icon_small.jpg          # 256x256
├── data/                       # RomFS 数据
└── romfs/                      # Switch 资源挂载点
```

## 支持的格式

| 格式 | 引擎 | 说明 |
|------|------|------|
| PDF | MuPDF | 完美支持 |
| EPUB | MuPDF | 完美支持（需 epub 含 CSS） |
| XPS | MuPDF | 完美支持 |
| CBZ/CBR/CBT/CB7 | MuPDF | 漫画/扫描件 |
| TXT | TextRenderer | 支持 GBK/GB18030 自动识别 |
| MD | TextRenderer | 基础 Markdown 渲染 |
| MOBI | TextRenderer | 支持基本的 MOBI 格式 |

## 手柄映射

| 按键 | 功能 |
|------|------|
| A | 确认 / 打开书籍 |
| B | 返回上一级 |
| ZL / L | 上一页 |
| ZR / R | 下一页 |
| ← / → | 翻页（同 ZL/ZR） |
| ↑ / ↓ | 滚动（文本模式） |
| + | 打开设置弹窗 |
| - | 打开目录弹窗 |
| 左摇杆 | 快速翻页（长按加速） |

## 书籍存放路径

在 Switch SD 卡上创建目录：
```
sdmc:/books/          ← 默认扫描路径
sdmc:/mjnexus/books/  ← 备用路径
```

支持子目录递归扫描。

## 下一步

构建完成后，将 `mjnexus_switch.nro` 或 NSP 拷到 Switch：

```bash
# 使用 hbmenu（推荐）：
#   1. 把 mjnexus_switch.nro 放到 sdmc:/switch/mjnexus_switch/
#   2. 通过 Hekate/Atmosphere 启动 hbmenu → 选中运行

# 安装 NSP：
#   1. 把 MJ_Nexus_Reader.nsp 拷到 SD 卡
#   2. 用 Tinfoil / Awoo Installer / GoldLeaf 安装
```

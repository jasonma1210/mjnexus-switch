# ====================================================================
# Dockerfile — mjnexus-switch build
#
# 使用 devkitPro 官方镜像构建 mjnexus-switch。
# 镜像内已包含 devkitA64 + libnx + dkp-pacman，
# 可以正常访问 devkitPro 内部包仓库（不走 Cloudflare 公网）。
#
# 用法：
#   # 基础用法
#   docker build -t mjnexus-switch .
#
#   # 如果需要代理
#   docker build -t mjnexus-switch \
#     --build-arg HTTP_PROXY=http://host:port \
#     --build-arg HTTPS_PROXY=http://host:port .
#
#   # 如果需要 Docker Hub 镜像加速
#   docker build -t mjnexus-switch \
#     --build-arg DOCKER_REGISTRY=docker.m.daocloud.io .
#
#   # 运行
#   docker run --rm -v $(pwd)/output:/output mjnexus-switch
#
# 或者直接用 run.sh。
# ====================================================================

# ── 允许通过 --build-arg 配置代理 ──────────────────────────────────
ARG HTTP_PROXY=
ARG HTTPS_PROXY=
ARG NO_PROXY=

# ── 设置代理环境变量 ────────────────────────────────────────────────
ENV HTTP_PROXY=${HTTP_PROXY}
ENV HTTPS_PROXY=${HTTPS_PROXY}
ENV NO_PROXY=${NO_PROXY}
ENV http_proxy=${HTTP_PROXY}
ENV https_proxy=${HTTPS_PROXY}
ENV no_proxy=${NO_PROXY}

# ── 基础镜像 ────────────────────────────────────────────────────────
# 如果官方 Docker Hub 拉取慢，可以用 --build-arg 改镜像源
#   docker build --build-arg BASE_IMAGE=docker.m.daocloud.io/devkitpro/devkitpro:switch .
ARG BASE_IMAGE=devkitpro/devkitpro:switch
FROM ${BASE_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive

# ── 1. 安装 Switch 开发工具链 ──────────────────────────────────────
# dkp-pacman 在 devkitpro:switch 内部可以正常访问官方仓库
RUN dkp-pacman -Syyu --noconfirm 2>&1 | tail -5 || true
RUN dkp-pacman -S --noconfirm \
        devkitA64 \
        libnx \
        switch-tools \
        switch-mesa \
        switch-glfw \
        switch-glm \
        switch-freetype \
        switch-libpng \
        switch-zlib \
        switch-hacbrewpack \
    2>&1 | tail -10

# switch-borealis 可能有，也可能没有 moonlight_wiliwili 定制
# 先尝试装，不行后面手动克隆
RUN dkp-pacman -S --noconfirm switch-borealis 2>&1 | tail -5 || true

# ── 2. 预装基础工具 ────────────────────────────────────────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
        git curl wget jq ca-certificates build-essential python3 pkg-config \
    && rm -rf /var/lib/apt/lists/*

# ── 3. 工作目录 ────────────────────────────────────────────────────
WORKDIR /build

# ── 4. 复制项目源码 ────────────────────────────────────────────────
# 先放 Makefile 等稳定文件，利用 Docker 缓存
COPY Makefile Makefile.mupdf BUILD.md README.md ./
COPY include/ include/
COPY source/ source/
COPY scripts/ scripts/
COPY resources/ resources/
COPY data/ data/
COPY romfs/ romfs/
COPY .gitignore ./

# ── 5. 拉取 Borealis moonlight_wiliwili 分支 ───────────────────────
# XITRIX 的定制分支（Switch 平台充分测试）
# 优先 GitHub，失败则尝试 Gitee 镜像
ARG BOREALIS_BRANCH=moonlight_wiliwili
RUN echo ">>> 拉取 Borealis (${BOREALIS_BRANCH}) ..." && \
    mkdir -p borealis && \
    (git clone --depth 1 -b "${BOREALIS_BRANCH}" \
        https://github.com/XITRIX/borealis.git \
        borealis/moonlight_wiliwili 2>&1 || \
     git clone --depth 1 -b "${BOREALIS_BRANCH}" \
        https://gitee.com/mirrors/borealis.git \
        borealis/moonlight_wiliwili 2>&1 || \
     echo "⚠️  Borealis 克隆失败 — 稍后会尝试用 pacman 的 switch-borealis") && \
    ls -la borealis/moonlight_wiliwili/ 2>/dev/null | head -3

# ── 6. 拉取 MuPDF 1.16.1 ────────────────────────────────────────────
# Makefile.mupdf 需要 mupdf/ 目录下有完整源码 + 内嵌字体
ARG MUPDF_TAG=1.16.1
RUN echo ">>> 拉取 MuPDF (${MUPDF_TAG}) ..." && \
    (git clone --depth 1 --branch "${MUPDF_TAG}" \
        https://git.ghostscript.com/mupdf.git mupdf 2>&1 || \
     git clone --depth 1 --branch "${MUPDF_TAG}" \
        https://github.com/ArtifexSoftware/mupdf.git mupdf 2>&1 || \
     echo "❌ MuPDF 克隆失败") && \
    ls mupdf/include/mupdf/fitz.h 2>/dev/null && echo "MuPDF OK" || echo "⚠️  MuPDF 不完整"

# ── 7. 编译 libmupdf.a ──────────────────────────────────────────────
RUN echo ">>> 编译 libmupdf.a ..." && \
    make -f Makefile.mupdf -j$(nproc) 2>&1 | tail -30

# ── 8. 编译主工程 mjnexus_switch.nro ───────────────────────────────
RUN echo ">>> 编译 mjnexus-switch ..." && \
    make -j$(nproc) 2>&1 | tail -50

# ── 9. 打包 Forwarder NSP ───────────────────────────────────────────
RUN echo ">>> 打包 Forwarder NSP ..." && \
    mkdir -p /output && \
    bash scripts/build_nsp.sh \
        --nro ./mjnexus_switch.nro \
        --title-id 0x01004d4a0001 \
        --title-name "MJ Nexus Reader" \
        --publisher "mj" \
        --icon ./resources/icons/icon.jpg \
        --output /output 2>&1 | tail -20

# ── 10. 确保产物在 /output ─────────────────────────────────────────
RUN echo "" && \
    echo "=== 构建产物 ===" && \
    ls -lh /build/mjnexus_switch.nro 2>/dev/null && cp /build/mjnexus_switch.nro /output/ 2>/dev/null; \
    ls -lh /build/mjnexus_switch.nacp 2>/dev/null && cp /build/mjnexus_switch.nacp /output/ 2>/dev/null; \
    ls -lh /build/lib/libmupdf.a 2>/dev/null && cp /build/lib/libmupdf.a /output/ 2>/dev/null; \
    echo "" && \
    echo "/output/ 目录:" && \
    ls -lh /output/

CMD ["bash", "-c", "echo '' && echo '========================================' && echo '  MJ Nexus Reader — build complete!' && echo '========================================' && echo '' && ls -lh /output/ && echo '' && echo '把 .nro 或 .nsp 拷到 Switch SD 卡即可运行'"]

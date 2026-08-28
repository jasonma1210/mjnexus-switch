// ============================================================
// mjnexus-switch —— 主入口
//
// Switch 自制软件的典型流程：
//   1. appletInit()    —— 初始化 libnx 应用环境；失败只能直接退出
//   2. consoleInit()   —— 初始化调试控制台（Switch 上可选，但推荐）
//   3. brls::Application::init() —— 初始化 Borealis 图形 / 输入系统
//   4. App::get().initialize()   —— 加载设置 + 应用主题
//   5. 把首页 push 到 View 栈，进入主循环
//   6. 退出前调用 close_book() 保存进度
//
// 注意：Makefile 里有 -fno-exceptions，不要用 try/catch。
//       所有错误只能通过返回值 / log 输出处理。
// ============================================================

#include <cstdio>
#include <cstdlib>

// ---- libnx ----
#include <switch.h>

// ---- Borealis ----
#include <borealis.hpp>

// ---- mjnexus ----
#include "app/App.hpp"
#include "app/HomePage.hpp"

// ============================================================
// main
// ============================================================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    // --- libnx ---
    Result rc = appletInit();
    if (R_FAILED(rc)) {
        return EXIT_FAILURE;
    }

    // 调试输出（Switch 上有一个小窗口显示 stdout；不影响 release）
    consoleInit(NULL);

    std::fprintf(stderr, "[mjnexus] starting ...\n");

    // --- Borealis ---
    // 主机模式 1920x1080；brls 会自动缩放到掌机 1280x720
    // "glfw" 后端名在 switch-mesa 上是合适的；
    // 如果以后换成 "deko" 也可以改这里
    brls::Application::init("mjnexus",
                             mjnexus::NINTENDO_SWITCH_SCREEN_W,
                             mjnexus::NINTENDO_SWITCH_SCREEN_H,
                             "glfw");

    // --- 应用运行时 ---
    mjnexus::App::get().initialize();

    // --- 首页 ---
    brls::Application::pushView(new mjnexus::HomePage(),
                                 brls::ViewStack::Default);

    // --- 主循环（阻塞直到用户点退出） ---
    brls::Application::run();

    // --- 退出清理 ---
    mjnexus::App::get().close_book();

    brls::Application::destroy();

    appletExit();
    return EXIT_SUCCESS;
}

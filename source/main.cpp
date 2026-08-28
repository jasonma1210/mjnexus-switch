#include <switch.h>
#include <borealis.hpp>
#include <cstdio>
#include <string>

namespace mjnexus
{
    class HomeActivity; // 前向声明
}

int main(int argc, char* argv[])
{
    // 初始化 Borealis（无参数）
    if (!brls::Application::init())
    {
        brls::Logger::error("Borealis init failed!");
        return -1;
    }

    // 创建窗口
    brls::Application::createWindow("MJ Reader");

    // 创建首页 Activity（传入 HomePage View）
    // 注意：Activity 构造需要 View*，稍后创建
    // brls::Application::pushActivity(new mjnexus::HomeActivity());

    // 主循环
    while (brls::Application::mainLoop())
    {
        // 可以在此处理额外逻辑
    }

    brls::Application::getPlatform()->exit();
    return 0;
}

// 注意：不 include switch.h！moonlight_wiliwili 用 SDL 跨平台
#include <borealis.hpp>
#include <cstdio>

int main(int argc, char* argv[])
{
    if (!brls::Application::init())
    {
        brls::Logger::error("Borealis init failed!");
        return -1;
    }

    brls::Application::createWindow("MJ Reader");

    // TODO: push HomeActivity
    // brls::Application::pushActivity(new HomeActivity());

    while (brls::Application::mainLoop())
    {
        // mainLoop 返回 false 时自然退出
    }

    // 不需要主动 exit，直接 return
    return 0;
}

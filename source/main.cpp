#include <switch.h>
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
        // 空循环，Activity 管理 UI
    }

    brls::Application::exit();
    return 0;
}

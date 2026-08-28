#pragma once
#include <borealis.hpp>

namespace mjnexus
{
    class SettingsPage : public brls::Box
    {
    public:
        SettingsPage();

        void draw(NVGcontext* vg, float x, float y, float width, float height,
                  brls::Style style, brls::FrameContext* ctx) override;
    };
}

#pragma once
#include <borealis.hpp>
#include <mjnexus/Config.hpp>
#include <mjnexus/Theme.hpp>
#include <string>
#include <vector>

namespace mjnexus
{
    class HomePage : public brls::Box
    {
    public:
        HomePage();

        void draw(NVGcontext* vg, float x, float y, float width, float height,
                  brls::Style style, brls::FrameContext* ctx) override;
    };
}

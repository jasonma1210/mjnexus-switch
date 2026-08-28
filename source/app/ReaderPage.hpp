#pragma once
#include <borealis.hpp>
#include <string>

namespace mjnexus
{
    class ReaderPage : public brls::Box
    {
    public:
        ReaderPage(const std::string& path = "");

        void draw(NVGcontext* vg, float x, float y, float width, float height,
                  brls::Style style, brls::FrameContext* ctx) override;

    private:
        std::string bookPath;
    };
}

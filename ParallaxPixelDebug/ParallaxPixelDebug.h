#pragma once

#include "Utils/Debug/PixelDebug.h"

namespace Falcor
{
class ParallaxPixelDebug : public PixelDebug
{
public:
    ParallaxPixelDebug(ref<Device> pDevice, uint32_t printCapacity = 4096, uint32_t assertCapacity = 100);
    ~ParallaxPixelDebug();

    void renderUI(Gui::Widgets& widget) { renderUI(&widget); }
    void renderUI(Gui::Widgets* widget = nullptr);

    void prepareProgram(const ref<Program>& pProgram, const ShaderVar& var);

    inline bool isEnabled() const noexcept { return mEnabled; }

protected:
    std::vector<float> mHeightValues;
    std::vector<float> mCellBoundaryParams;
    std::vector<float> mHeightSamples;
    uint3 mTextureDims = { 0, 0, 0 };
    int mSelectedMipLevel=0;
};
}

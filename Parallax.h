/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#pragma once
#include "Falcor.h"
#include "Core/SampleApp.h"
#include "ComputeProgramWrapper.h"
#include "ParallaxPixelDebug/ParallaxPixelDebug.h"

using namespace Falcor;

class Parallax : public SampleApp
{
public:
    Parallax(const SampleAppConfig& config);

    void onLoad(RenderContext* pRenderContext) override;
    void onFrameRender(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo) override;
    void onShutdown() override;
    void onResize(uint32_t width, uint32_t height) override;
    bool onKeyEvent(const KeyboardEvent& keyEvent) override;
    bool onMouseEvent(const MouseEvent& mouseEvent) override;
    bool onGamepadState(const GamepadState& gamepadState) override;
    void onHotReload(HotReloadFlags reloaded) override;
    void onGuiRender(Gui* pGui) override;

private:

    void guiTexureInfo(Gui::Widgets& w);
    void guiRenderSettings(Gui::Widgets& w);
    void guiProceduralGeneration(Gui::Widgets& w);
    void guiConemapGeneration(Gui::Widgets& w);
    void guiQuickconemapGeneration(Gui::Widgets& w);
    void guiLoadImage(Gui::Widgets& w);
    void guiDebugRender(Gui::Widgets& w);
    void guiSaveImage(Gui::Widgets& w);
    void guiPixelDebug(Gui::Widgets& w);

    // program
    ref<Program>         mpDebugProgram = nullptr;
    ref<ProgramVars>     mpDebugVars = nullptr;
    ref<GraphicsState>   mpDebugRenderState = nullptr;

    ref<Program>         mpParallaxProgram = nullptr;
    ref<ProgramVars>     mpParallaxVars = nullptr;
    ref<GraphicsState>   mpParallaxRenderState = nullptr;
    // compute
    ref<ComputeProgramWrapper> mpHeightmapCompute = nullptr;
    bool mRunHeightmapCompute = false;
    struct ProceduralHeightmapComputeSettings {
        uint2 newHMapSize = { 512, 512 };
        bool newHmap16bit = true;
        uint32_t heightFun = 0;
        int4 HMapIntParams = { 5, 0, 0, 0 };
        float4 HMapFloatParams = { 1.0f, 0.0f, 0.0f, 0.0f };
    } mProcHMCompSettings;
    
    ref<ComputeProgramWrapper> mpConemapCompute = nullptr;
    ref<ComputeProgramWrapper> mpConemapPostprocess = nullptr;
    bool mRunConemapCompute = false;
    struct ConemapComputeSettings {
        bool newHmap16bit = true;
        bool POSTPROCESS_MIN = false;
        bool DO_SQRT_LOOKUP = false;
        uint relaxedConeSearchSteps = 64;
        std::string algorithm = "1";
        std::string name = "";
    } mCMCompSettings;

    ref<ComputeProgramWrapper> mpTextureCopyCompute = nullptr;

    ref<ComputeProgramWrapper> mpMinmaxCopyCompute = nullptr;
    ref<ComputeProgramWrapper> mpMinmaxMipmapCompute = nullptr;
    bool mRunMinmaxCompute = false;

    ref<ComputeProgramWrapper> mpQuickConemapCompute = nullptr;
    bool mRunQuickConemapCompute = false;
    struct QuickConemapComputeSettings {
        bool newHmap16bit = true;
        bool POSTPROCESS_MIN = false;
        bool maxAtTexelCenter = false;
        std::string algorithm = "2";
        std::string name = "";
    } mQCMCompSettings;
    // geometry
    ref<Buffer> mpVertexBuffer = nullptr;
    ref<Vao> mpVao = nullptr;
    // camera
    ref<Camera> mpCamera = nullptr;
    FirstPersonCameraController mCameraController;
    void resetCamera();
    // texture
    ref<Texture> mpHeightmapTex = nullptr;
    std::filesystem::path mHeightmapName = getRuntimeDirectory() / "data/Gravel_05/Gravel_05_height_512.png";
    void LoadHeightmapTexture(); // load texture from file(mHeightmapName) and set variables
    ref<Texture> mpAlbedoTex = nullptr;
    std::filesystem::path mAlbedoName = getRuntimeDirectory() / "data/Gravel_05/Gravel_05_diffuse_1K.png";
    void LoadAlbedoTexture(); // load texture from file(mAlbedoName) and set variables

    bool mGenerateMips = false;
    ref<Sampler> mpSampler = nullptr;
    ref<Sampler> mpSamplerNearest = nullptr;
    ref<Texture> mpConeTex = nullptr;
    ref<Texture> mpMinmaxTex = nullptr;

    ref<ComputeProgramWrapper> mpMaxCompute = nullptr;

    bool mRunQDMCompute = false;
    ref<Texture> mpQDMTex = nullptr;
    ref<Texture> generateQDMMap(const ref<Texture>& pHeightmap, RenderContext* pRenderContext) const;
    void Parallax::guiQDMGeneration(Gui::Widgets& parent);

    ref<ComputeProgramWrapper> mpInitMaxMipCompute = nullptr;
    ref<ComputeProgramWrapper> mpCalcMaxMipCompute = nullptr;
    bool mRunMaxMipCompute = false;
    ref<Texture> mpMaxMipTex = nullptr;
    ref<Texture> generateMaxMipMap(const ref<Texture>& pHeightmap, RenderContext* pRenderContext) const;
    void Parallax::guiMaxMipGeneration(Gui::Widgets& parent);

    std::filesystem::path saveFilePath = "";
    int doSaveTexture = 0;
    ref<Texture> mpDebugTex = nullptr; // the texture that is drawn in debug mode
    // texture debug
    struct DebugSettings {
        bool drawDebug = false;
        uint32_t mipLevel = 0;
        uint32_t slice = 0;
        uint3 debugChannels = { 0, 1, 2 };
    } mDebugSettings;

    // render settings
    friend struct RenderSettings;
    struct RenderSettings {
        RenderSettings(Parallax& app) : app(app) {}
        Parallax& app;
        float3 lightDir{ normalize(float3(.198f, -.462f, -.865f)) };
        float heightMapHeight = 0.2f;
        bool discardFragments = true;
        bool displayNonConverged = true;
        float lightIntensity = 1.0f;
        uint stepNum = 200;
        uint refineStepNum = 5;
        float3 scale{ 1, 1, 1 };
        float angle = 0.0f;
        float3 axis{ 0, 0, 1 };
        float3 translate{ 0, 0, 0 };
        float relax = 1.0f;
        bool BILINEAR_BY_HAND = false;
        bool CONSERVATIVE_STEP = false;
        uint32_t selectedParallaxFun = 3; // see kParallaxFunList
        void setParallaxFun();
        uint32_t selectedRefinementFun = 0; // see kRefineFunList
        void setRefinementFun();
    } mRenderSettings;


    // compute calls
    ref<Texture> generateProceduralHeightmap(const ProceduralHeightmapComputeSettings& settings, RenderContext* pRenderContext) const;
    ref<Texture> generateConemap(const ConemapComputeSettings& settings, const ref<Texture>& pHeightmap, RenderContext* pRenderContext) const;
    ref<Texture> generateMinmaxMipmap(const ref<Texture>& pHeightmap, RenderContext* pRenderContext) const;
    ref<Texture> generateQuickConemap(const QuickConemapComputeSettings& settings, const ref<Texture>& pMinmaxMipmap, RenderContext* pRenderContext) const;

    // Pixeld Debug
    ParallaxPixelDebug mPixelDebug;






    
    std::string getModelAndSettingsString();
    
    class ScreenCapture
    {
    public:
        void captrueNextFrame(std::string fileName = "", std::string directory = "");
        void captureIfRequested(const ref<Fbo>& pTargetFbo);

    private:
        bool mDoCapture = false;
        std::string mFileName;
        std::string mDirectory;
    } mScreenCapture;




};

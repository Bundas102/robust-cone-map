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
#include "Core/Macros.h"
#include "Utils/Image/ImageIO.h"
#include "Core/Program/ProgramManager.h"
#include "Core/AssetResolver.h"
#include "Parallax.h"
#include <string>
using namespace std::string_literals;

struct Vertex
{
    float3 pos;
    float2 tex;
    float3 norm;
};
namespace
{
    const std::vector<Vertex> kVertices =
    {
        {float3(-1, 0, -1), float2(0, 0), float3(0, 1, 0)},
        {float3(-1, 0, +1), float2(0, 1), float3(0, 1, 0)},
        {float3(+1, 0, -1), float2(1, 0), float3(0, 1, 0)},
        {float3(+1, 0, +1), float2(1, 1), float3(0, 1, 0)},
    };
    const char kParallaxFunDefine[] = "PARALLAX_FUN";
    const Gui::DropdownList kParallaxFunList = {
        {0, "0: Bump mapping"},
        {1, "1: Parallax mapping"},
        {2, "2: Linear search"},
        {3, "3: Cone step mapping"},
        {10, "4(10): Seidel's Maximum Mip tracing"},
        {11, "5(11): Drobot's QDM tracing"},
    };
    const char kRefinementFunDefine[] = "REFINE_FUN";
    const Gui::DropdownList kRefinementFunList = {
        {0, "0: No refinement"},
        {1, "1: Linear approx"},
        {2, "2: Binary search"},
    };
    const char kHeightFunDefine[] = "HEIGHT_FUN";
    const Gui::DropdownList kHeightFunList = {
        {0, "Sinc"},
        {1, "Spheres"},
        {2, "Edge"},
        {3, "Dot"}
    };
    const char kConeTypeDefine[] = "CONE_TYPE";
    const char kQuickGenAlgDefine[] = "QUICK_GEN_ALG";
    const char kDebugModeDefine[] = "DEBUG_MODE";
    const char kMaxAtTexelCenterDefine[] = "MAX_AT_TEXEL_CENTER";
    const Gui::DropdownList kDebugTexList = {
        {0, "Heightmap"},
        {1, "Conemap"},
        //{2, "Min max"},
        {3, "Loaded albedo map"},
        {4, "Max mip map"},
        {5, "QDM"},
    };
    const Gui::DropdownList kDebugChannelList = {
        {0, "X RED"},
        {1, "Y GREEN"},
        {2, "Z BLUE"},
        {3, "W ALPHA"},
        {4, "0 ZERO"},
        {5, "H HALF"},
        {6, "1 ONE"},
    };
   
}

std::string filenameFromPath(const std::string& path)
{
    size_t pos = path.find_last_of("\\/");
    return (std::string::npos != pos) ? path.substr(pos + 1) : path;
}

std::string TextureDescription(const Texture* const tex) {
    if (!tex) return "  [ no texture ]";

    const auto& format = tex->getFormat();
    return " * Resolution: " + std::to_string(tex->getWidth()) + " x " + std::to_string(tex->getHeight()) +
           (tex->getDepth() > 1 ? " x " + std::to_string(tex->getDepth()) : "") + 
        "\n * MIP: " + std::to_string(tex->getMipCount()) +
        "\n * Format: " + to_string(format) +
        "\n * Source: " + tex->getName();
}

Parallax::Parallax(const SampleAppConfig& config)
    : SampleApp(config)
    , mpCamera(Camera::create("Square Viewer Camera")), mCameraController(mpCamera), mRenderSettings(*this)
    , mPixelDebug(getDevice())
{}



void Parallax::onGuiRender(Gui* pGui)
{
    Gui::Window w(pGui, "Parallax playground", { 420, 900 });
    //renderGlobalUI(pGui);
    //w.separator();

    auto cameraGroup = Gui::Group(w, "Camera Controls");
    if (cameraGroup.open())
    {
        if (cameraGroup.button("Reset camera"))
        {
            resetCamera();
        }
        mpCamera->renderUI(cameraGroup);
        cameraGroup.release();
    }
    w.separator();

    auto mainGroup = Gui::Group( w, "Parallax Controls", true );
    if ( mainGroup.open() )
    {
        if (w.dropdown(kParallaxFunDefine, kParallaxFunList, mRenderSettings.selectedParallaxFun))
        {
            mRenderSettings.setParallaxFun();
        }
        if (w.dropdown(kRefinementFunDefine, kRefinementFunList, mRenderSettings.selectedRefinementFun))
        {
            mRenderSettings.setRefinementFun();
        }
        w.separator();
        guiTexureInfo(mainGroup);
        w.separator();
        guiRenderSettings(mainGroup);
        w.separator();
        guiProceduralGeneration(mainGroup);
        w.separator();
        guiConemapGeneration(mainGroup);
        w.separator();
        guiQuickconemapGeneration(mainGroup);
        w.separator();
        guiMaxMipGeneration(mainGroup);
        w.separator();
        guiQDMGeneration(mainGroup);
        w.separator();
        guiLoadImage(mainGroup);
        w.separator();
        guiDebugRender(mainGroup);
        w.separator();
        guiSaveImage(mainGroup);

        mainGroup.release();
    }
    w.separator();

    // V-sync toggleing doesn't seem to work in this version of Falcor
    // bool isVsyncOn = isVsyncEnabled();
    // if(w.checkbox("VSync", isVsyncOn)){
    //     toggleVsync(isVsyncOn);
    // }
    // w.separator();

    guiPixelDebug(w);
}

void Parallax::guiTexureInfo(Gui::Widgets& parent)
{
    auto w = Gui::Group(parent, "Loaded Textures", true);
    if (!w.open())
        return;

    ShaderVar pParallaxVars = mpParallaxVars->getRootVar();

    // HEIGHTMAP
    if ((mpHeightmapTex || w.button("---"))  && w.button("Use##heightmap"))
    {
        pParallaxVars["gTexture"] = mpHeightmapTex;
    }
    w.text("HEIGHTMAP", true);
    w.tooltip("Don't forget to set `PARALLAX_FUN`");
    {
        static const Texture* last = nullptr;
        static std::string sizeText = TextureDescription(last);
        if (last != mpHeightmapTex.get()) {
            last = mpHeightmapTex.get();
            sizeText = TextureDescription(last);
        }
        const auto& tx = pParallaxVars["gTexture"].getTexture();
        if (tx && tx.get() == last) w.text("IN USE", true);
        w.text(sizeText);
    }
    // CONEMAP
    if ((mpConeTex || w.button("---"))  && w.button("Use##conemap"))
    {
        pParallaxVars["gTexture"] = mpConeTex;
    }
    w.text("CONEMAP", true);
    w.tooltip("Don't forget to set `PARALLAX_FUN`");
    {
        static const Texture* last = nullptr;
        static std::string sizeText = TextureDescription(last);
        if (last != mpConeTex.get()) {
            last = mpConeTex.get();
            sizeText = TextureDescription(last);
        }
        const auto& tx = pParallaxVars["gTexture"].getTexture();
        if (tx && tx.get() == last) w.text("IN USE", true);
        w.text(sizeText);
    }
    if ((mpQDMTex || w.button("---")) && w.button("Use##QDM"))
    {
        pParallaxVars["gTexture"] = mpQDMTex;
    }
    w.text("QDM MAP", true);
    w.tooltip("Don't forget to set `PARALLAX_FUN`");
    {
        static const Texture* last = nullptr;
        static std::string sizeText = TextureDescription(last);
        if (last != mpQDMTex.get())
        {
            last = mpQDMTex.get();
            sizeText = TextureDescription(last);
        }
        const auto& tx = pParallaxVars["gTexture"].getTexture();
        if (tx && tx.get() == last)
            w.text("IN USE", true);
        w.text(sizeText);
    }
    if ((mpMaxMipTex || w.button("---"))  && w.button("Use##MaxMip"))
    {
        // WARNING: different texture
        pParallaxVars["MaxMipTexture"] = mpMaxMipTex;
    }
    w.text("MaxMip MAP", true);
    w.tooltip("Don't forget to set `PARALLAX_FUN`");
    {
        static const Texture* last = nullptr;
        static std::string sizeText = TextureDescription(last);
        if (last != mpMaxMipTex.get())
        {
            last = mpMaxMipTex.get();
            sizeText = TextureDescription(last);
        }
        const auto& tx = pParallaxVars["MaxMipTexture"].getTexture(); // WARNING: Different texture
        if (tx && tx.get() == last)
        {
            w.text("IN USE", true);
            w.tooltip("Always active because it uses a different binding point");
        }
        w.text(sizeText);
    }
    // ALBEDO
    if (w.button("Use##use__albedo")) {
        getDevice()->getProgramManager()->addGlobalDefines({{"USE_ALBEDO_TEXTURE", "1"}});
    }
    if (w.button("Unuse##unuse__albedo", true)) {
        getDevice()->getProgramManager()->addGlobalDefines({{"USE_ALBEDO_TEXTURE", "0"}});
    }
    w.text("ALBEDO", true);
    w.tooltip("Use/unuse the loaded albedo texture");
    {
        static const Texture* last = nullptr;
        static std::string sizeText = TextureDescription(last);
        if (last != mpAlbedoTex.get()) {
            last = mpAlbedoTex.get();
            sizeText = TextureDescription(last);
        }
        w.text(sizeText);
    }
    w.release();
}
void Parallax::guiRenderSettings(Gui::Widgets& parent)
{
    auto w = Gui::Group(parent, "Render Settings");
    if (!w.open())
        return;
    w.direction("Light directon", mRenderSettings.lightDir);
    w.tooltip("Directional light, world space\nPress L to set it to the camera view direction", true);
    w.slider("Light intensity", mRenderSettings.lightIntensity, 0.5f, 8.0f);
    w.slider("Heightmap height", mRenderSettings.heightMapHeight, .0f, .5f);
    w.tooltip("World space", true);
    w.checkbox("Discard fragments", mRenderSettings.discardFragments);
    w.tooltip("Discard fragments when the ray misses the Heightmap, otherwise the pixel is colored red.", true);
    w.checkbox("Debug: display non-converged", mRenderSettings.displayNonConverged);
    w.tooltip("Color fragments to magenta if the primary search is not converged.", true);
    w.slider("Max step number", mRenderSettings.stepNum, 2U, 200U);
    w.tooltip("Step number for iterative primary searches", true);
    w.slider("Max refine step number", mRenderSettings.refineStepNum, 0U, 20U);
    w.tooltip("Step number for iterative refinement searches", true);
    w.slider("Relax multiplier", mRenderSettings.relax, 1.0f, 8.0f);
    w.tooltip("Primary search step relaxation", true);
    if (w.checkbox("BILINEAR_BY_HAND", mRenderSettings.BILINEAR_BY_HAND))
    {
        mpParallaxProgram->addDefine("BILINEAR_BY_HAND", mRenderSettings.BILINEAR_BY_HAND ? "1" : "0");
    }
    if (w.checkbox("CONSERVATIVE_STEP", mRenderSettings.CONSERVATIVE_STEP))
    {
        mpParallaxProgram->addDefine("CONSERVATIVE_STEP", mRenderSettings.CONSERVATIVE_STEP ? "1" : "0");
    }
    w.separator();

    w.text("Transformation");
    w.slider("Scale", mRenderSettings.scale, 0.1f, 2.f);
    w.slider("Rot. angle", mRenderSettings.angle, 0.0f, 2 * 3.14159f);
    w.direction("Rot. axis", mRenderSettings.axis);
    w.slider("Translate", mRenderSettings.translate, -1.f, 1.f);
    w.release();
}
void Parallax::guiProceduralGeneration(Gui::Widgets& parent)
{
    auto w = Gui::Group(parent, "Procedural Heightmap Generation");
    if (!w.open())
        return;
    if (w.var("New Heightmap Size", mProcHMCompSettings.newHMapSize, 1, 1024)) {
        mProcHMCompSettings.newHMapSize = max(mProcHMCompSettings.newHMapSize, uint2(1));
    }
    w.checkbox("16 bit texture##procedural", mProcHMCompSettings.newHmap16bit);
    w.tooltip("Checked: R16Unorm, unchecked: R8Unorm");
    w.dropdown(kHeightFunDefine, kHeightFunList, mProcHMCompSettings.heightFun);
    w.var("Int parameters", mProcHMCompSettings.HMapIntParams);
    w.tooltip("x: num of hemispheres in a row\ny: unused\nz: unused\nw: unused");
    w.var("Float parameters", mProcHMCompSettings.HMapFloatParams, std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(), 0.01f, false, "%.2f");
    w.tooltip("x: relative radius (1: the spheres touch)\ny: unused\nz: unused\nw: unused");
    if (w.button("Generate procedural Heightmap") && mpHeightmapTex && mpHeightmapCompute)
    {
        mRunHeightmapCompute = true;
        getDevice()->getProgramManager()->addGlobalDefines({{"USE_ALBEDO_TEXTURE", "0"}});
        mpConeTex.reset();
        mpMinmaxTex.reset();
        mpQDMTex.reset();
        mpMaxMipTex.reset();
    }
    w.tooltip("Generates a Heightmap; deletes the Conemap");
    w.release();
}
void Parallax::guiConemapGeneration(Gui::Widgets& parent)
{
    auto w = Gui::Group(parent, "Conemap Generation from Heightmap");
    if (!w.open())
        return;
    w.checkbox("16 bit texture##conemap", mCMCompSettings.newHmap16bit);
    w.tooltip("Checked: R16Unorm, unchecked: R8Unorm");
    w.checkbox("Store aperture sqrt", mCMCompSettings.DO_SQRT_LOOKUP);
    w.tooltip("Checked: sqrt(minTan), unchecked: minTan is stored in conemap");

    w.checkbox("POSTPROCESS_MIN##conemap", mCMCompSettings.POSTPROCESS_MIN);
    w.tooltip(
        "Guarantees conservative bilinear interpolation for cones.\n"
        "Works with relaxed and simple cone maps (if they were correctly generated)"
    );
    if (w.button("Generate Conemap from Heightmap") && mpHeightmapTex && mpConemapCompute)
    {
        mRunConemapCompute = true;
        mCMCompSettings.algorithm = "1";
        mCMCompSettings.name = "ConeMap";
        if (mCMCompSettings.POSTPROCESS_MIN)
            mCMCompSettings.name += "-PostProcessed";
    }
    w.tooltip("Single dispatch; might crash for larger textures.");

    
    if (w.button("Generate Correct Relaxed Conemap (NEW)") && mpHeightmapTex && mpConemapCompute)
    {
        mRunConemapCompute = true;
        mCMCompSettings.algorithm = "4";
        mCMCompSettings.name = "CorrectMap";
        if (mCMCompSettings.POSTPROCESS_MIN)
            mCMCompSettings.name += "-PostProcessed";
    }
    w.tooltip("Single dispatch; might crash for larger textures.");
    {
        static bool everyFrame = false;
        //w.checkbox("gen every frame##conemap", everyFrame);
        if (everyFrame)
        {
            mRunConemapCompute = true;
        }
    }
    w.var("Relaxed Cone Search Steps", mCMCompSettings.relaxedConeSearchSteps, 2u);
    if (w.button("Generate Relaxed Conemap from Heightmap") && mpHeightmapTex && mpConemapCompute)
    {
        mRunConemapCompute = true;
        mCMCompSettings.algorithm = "2";
        mCMCompSettings.name = "RelaxedMap-" + std::to_string(mCMCompSettings.relaxedConeSearchSteps);
        if (mCMCompSettings.POSTPROCESS_MIN)
            mCMCompSettings.name += "-PostProcessed";
    }
    w.tooltip("Single dispatch; might crash for larger textures.");
    w.release();
}
void Parallax::guiQuickconemapGeneration(Gui::Widgets& parent)
{
    auto w = Gui::Group(parent, "Quick Conemap Generation from Heightmap");
    if (!w.open())
        return;
    w.checkbox("16 bit texture##quickconemap", mQCMCompSettings.newHmap16bit);
    w.tooltip("Checked: R16Unorm, unchecked: R8Unorm");
    w.checkbox("Max at texel centers", mQCMCompSettings.maxAtTexelCenter);
    w.tooltip("Texel center heuristic", true);
    w.checkbox("POSTPROCESS_MIN##quickconemap", mQCMCompSettings.POSTPROCESS_MIN);
    w.tooltip(
        "Guarantees conservative bilinear interpolation for cones.\n"
        "Works with relaxed and simple cone maps (if they were correctly generated)"
    );

    {
        static bool everyFrame = false;
        //w.checkbox("gen every frame##quickconemap", everyFrame);
        if (everyFrame)
        {
            mRunMinmaxCompute = true;
            mRunQuickConemapCompute = true;
        }
    }
    if (w.button("Generate Quick Conemap - naive") && mpQuickConemapCompute)
    {
        mRunMinmaxCompute = true;
        mRunQuickConemapCompute = true;
        mQCMCompSettings.algorithm = "1";
        mQCMCompSettings.name = "Naive Quick Conemap"s + (mQCMCompSettings.maxAtTexelCenter ? " + Center Heuristic" : "");
        if (mQCMCompSettings.POSTPROCESS_MIN)
            mQCMCompSettings.name += " + PostProcessed";
    }
    if (w.button("Generate Quick Conemap - region growing 3x3") && mpQuickConemapCompute)
    {
        mRunMinmaxCompute = true;
        mRunQuickConemapCompute = true;
        mQCMCompSettings.algorithm = "2";
        mQCMCompSettings.name = "Quick Conemap"s + (mQCMCompSettings.maxAtTexelCenter ? " + Center Heuristic" : "");
        if (mQCMCompSettings.POSTPROCESS_MIN)
            mQCMCompSettings.name += " + PostProcessed";
    }
    w.release();
}
void Parallax::guiQDMGeneration(Gui::Widgets& parent)
{
    auto w = Gui::Group(parent, "Quadtree Displacement Mapping mip-map generation from Heightmap");
    if (!w.open())
        return;

    {
        static bool everyFrame = false;
        //w.checkbox("gen every frame##qdm", everyFrame);
        if (everyFrame)
        {
            mRunQDMCompute = true;
        }
    }
    if (w.button("Generate max mip-map##qdm"))
    {
        mRunQDMCompute = true;
    }
    w.release();
}
void Parallax::guiMaxMipGeneration(Gui::Widgets& parent)
{
    auto w = Gui::Group(parent, "Maximum Mip Mapping mip-map generation from Heightmap");
    if (!w.open())
        return;
    
    {
        static bool everyFrame = false;
        //w.checkbox("gen every frame##maxmip", everyFrame);
        if (everyFrame)
        {
            mRunMaxMipCompute = true;
        }
    }
    if (w.button("Generate max mip-map##maxmip"))
    {
        mRunMaxMipCompute = true;
    }
    
    w.release();
}
void Parallax::guiLoadImage(Gui::Widgets& parent)
{
    auto w = Gui::Group(parent, "Load Image");
    if (!w.open())
        return;
    w.checkbox("Generate Mipmaps on Image Load", mGenerateMips);
    bool reloadHeightmap = false;
    if (w.button("Choose Height File"))
    {
        reloadHeightmap |= openFileDialog({}, mHeightmapName);
    }
    w.tooltip("Loads texture to Heightmap; deletes the Conemap");

    bool reloadAlbedo = false;
    if (w.button("Choose Albedo File"))
    {
        reloadAlbedo |= openFileDialog({}, mAlbedoName);
    }
    w.dummy("", float2(10, 10), true);
    if (w.button("Use##use_albedo", true)) {
        //mpParallaxProgram->addDefine("USE_ALBEDO_TEXTURE", "1");
        getDevice()->getProgramManager()->addGlobalDefines({{"USE_ALBEDO_TEXTURE", "1"}});
    }
    if (w.button("Unuse##unuse_albedo", true)) {
        //mpParallaxProgram->addDefine("USE_ALBEDO_TEXTURE", "0");
        getDevice()->getProgramManager()->addGlobalDefines({{"USE_ALBEDO_TEXTURE", "0"}});
    }
    w.tooltip("Use/unuse the loaded albedo texture");


    if (reloadHeightmap && !mHeightmapName.empty())
    {
        // mHeightmapName = stripDataDirectories(mHeightmapName);
        LoadHeightmapTexture();
    }

    if (reloadAlbedo && !mAlbedoName.empty())
    {
        // mAlbedoName = stripDataDirectories(mAlbedoName);
        LoadAlbedoTexture();
    }
    w.release();
}
void Parallax::guiDebugRender(Gui::Widgets& parent)
{
    auto w = Gui::Group(parent, "Debug Texture View");
    if (!w.open())
        return;
    bool changed = w.checkbox("Debug: draw texture", mDebugSettings.drawDebug);
    if (mDebugSettings.drawDebug)
    {
        changed |= w.button("Re-assign debug texture", true);
        static uint32_t selectedTexId = 0;
        changed |= w.dropdown("debug texture", kDebugTexList, selectedTexId);
        if (changed) {
            switch (selectedTexId)
            {
            case 0: mpDebugTex = mpHeightmapTex; break;
            case 1: mpDebugTex = mpConeTex; break;
            case 2: mpDebugTex = mpMinmaxTex; break;
            case 3: mpDebugTex = mpAlbedoTex; break;
            case 4: mpDebugTex = mpMaxMipTex; break;
            case 5: mpDebugTex = mpQDMTex; break;
            }
        }
        w.slider("Mip level", mDebugSettings.mipLevel, (uint32_t)0, mpDebugTex ? mpDebugTex->getMipCount() - 1 : 0);
        w.slider("3D slice", mDebugSettings.slice, (uint32_t)0, mpDebugTex ? mpDebugTex->getDepth() - 1 : 0);

        w.dropdown("RED", kDebugChannelList, mDebugSettings.debugChannels.x);
        w.dropdown("GREEN", kDebugChannelList, mDebugSettings.debugChannels.y);
        w.dropdown("BLUE", kDebugChannelList, mDebugSettings.debugChannels.z);

        if (w.button("XXX"))       mDebugSettings.debugChannels = { 0, 0, 0 };
        if (w.button("YYY", true)) mDebugSettings.debugChannels = { 1, 1, 1 };
        if (w.button("ZZZ", true)) mDebugSettings.debugChannels = { 2, 2, 2 };
        if (w.button("WWW", true)) mDebugSettings.debugChannels = { 3, 3, 3 };
        if (w.button("X00"))       mDebugSettings.debugChannels = { 0, 4, 4 };
        if (w.button("0Y0", true)) mDebugSettings.debugChannels = { 4, 1, 4 };
        if (w.button("00Z", true)) mDebugSettings.debugChannels = { 4, 4, 2 };
        if (w.button("XYZ"))       mDebugSettings.debugChannels = { 0, 1, 2 };
        if (w.button("YZ0", true)) mDebugSettings.debugChannels = { 1, 2, 4 };
        if (w.button("ZW0", true)) mDebugSettings.debugChannels = { 2, 3, 4 };
    }
    w.release();
}
void Parallax::guiSaveImage(Gui::Widgets& parent)
{
    auto w = Gui::Group(parent, "Save to File");
    if (!w.open())
        return;
    if (w.button("Save Heightmap to texture") && mpHeightmapTex) {
        if (saveFileDialog({ {"exr","EXR"} }, saveFilePath)) {
            doSaveTexture = 1;
        }
    }
    if (w.button("Save Conemap to texture") && mpConeTex) {
        if (saveFileDialog({ {"exr","EXR"} }, saveFilePath)) {
            doSaveTexture = 2;
        }
    }
    w.release();
}

void Parallax::guiPixelDebug(Gui::Widgets& parent)
{
    auto w = Gui::Group(parent, "Pixel Debug",true);
    if (!w.open())
        return;
    mPixelDebug.renderUI(w);
}

void initSquare(ref<Device> pDevice, ref<Buffer>& pVB, ref<Vao>& pVao)
{
    sizeof(Vertex);
    // Create VB
    const uint32_t vbSize = (uint32_t)(sizeof(Vertex) * kVertices.size());
    pVB = pDevice->createBuffer(vbSize, ResourceBindFlags::Vertex, MemoryType::DeviceLocal, (void*)kVertices.data());
    assert(pVB);

    // Create VAO
    ref<VertexLayout> pLayout = VertexLayout::create();
    ref<VertexBufferLayout> pBufLayout = VertexBufferLayout::create();
    pBufLayout->addElement("POSITION", offsetof(Vertex, pos), ResourceFormat::RGB32Float, 1, 0);
    pBufLayout->addElement("TEXCOORD", offsetof(Vertex, tex), ResourceFormat::RG32Float, 1, 1);
    pBufLayout->addElement("NORMAL",   offsetof(Vertex, norm), ResourceFormat::RGB32Float, 1, 2);
    pLayout->addBufferLayout(0, pBufLayout);

    Vao::BufferVec buffers{ pVB };
    pVao = Vao::create(Vao::Topology::TriangleStrip, pLayout, buffers);
    assert(pVao);
}

void Parallax::RenderSettings::setParallaxFun() {
    app.getDevice()->getProgramManager()->addGlobalDefines({{kParallaxFunDefine, std::to_string(selectedParallaxFun)}});
}

void Parallax::RenderSettings::setRefinementFun() {
    app.getDevice()->getProgramManager()->addGlobalDefines({{kRefinementFunDefine, std::to_string(selectedRefinementFun)}});
}

void Parallax::onLoad(RenderContext* pRenderContext)
{
    getDevice()->getProgramManager()->addGlobalDefines(
            {
            {kParallaxFunDefine, std::to_string(mRenderSettings.selectedParallaxFun )},
            {kRefinementFunDefine, std::to_string(mRenderSettings.selectedRefinementFun )},
            }
    );
    getDevice()->getProgramManager()->setGlobalCompilerArguments({"-Wno-30081", "-Wno-15401", "-Wno-15205"});

    // parallax program
    {
        ProgramDesc d;
        d.addShaderLibrary( "Samples/Parallax/Parallax.vs.slang" ).vsEntry( "main" );
        d.addShaderLibrary( "Samples/Parallax/Parallax.ps.slang" ).psEntry( "main" );

        mpParallaxProgram = Program::create( getDevice(), d );
        mpParallaxProgram->addDefine("BILINEAR_BY_HAND", mRenderSettings.BILINEAR_BY_HAND ? "1" : "0");
        mpParallaxProgram->addDefine("CONSERVATIVE_STEP", mRenderSettings.CONSERVATIVE_STEP ? "1" : "0");
        mpParallaxProgram->addDefine("DO_SQRT_LOOKUP", mCMCompSettings.DO_SQRT_LOOKUP ? "1" : "0");
    }

    // debug texture program
    {
        ProgramDesc d;
        d.addShaderLibrary( "Samples/Parallax/Parallax.vs.slang" ).vsEntry( "main" );
        d.addShaderLibrary( "Samples/Parallax/ParallaxPixelDebug/ParallaxDebug.ps.slang" ).psEntry( "mainDebug" );

        mpDebugProgram = Program::create(getDevice(), d );
    }

    mpParallaxRenderState = GraphicsState::create(getDevice());
    mpParallaxRenderState->setProgram(mpParallaxProgram);
    mpParallaxVars = ProgramVars::create(getDevice(),mpParallaxProgram.get());
    assert(mpParallaxRenderState);

    mpDebugRenderState = GraphicsState::create(getDevice());
    mpDebugRenderState->setProgram( mpDebugProgram );
    mpDebugVars = ProgramVars::create(getDevice(), mpDebugProgram.get());
    assert( mpDebugRenderState );

    // compute
    mpHeightmapCompute = ComputeProgramWrapper::create(getDevice());
    mpHeightmapCompute->createProgram("Samples/Parallax/ProceduralHeightmap.cs.slang");

    mpConemapCompute = ComputeProgramWrapper::create(getDevice());
    mpConemapCompute->createProgram("Samples/Parallax/Conemap.cs.slang", "main", {
        {kConeTypeDefine, mCMCompSettings.algorithm},
        {"DO_SQRT_LOOKUP", mCMCompSettings.DO_SQRT_LOOKUP ? "1" : "0"}
    });
    mpConemapPostprocess = ComputeProgramWrapper::create(getDevice());
    mpConemapPostprocess->createProgram("Samples/Parallax/Conemap.cs.slang", "main_postprocess_max", {{kConeTypeDefine, mCMCompSettings.algorithm}});

    mpTextureCopyCompute = ComputeProgramWrapper::create(getDevice());
    mpTextureCopyCompute->createProgram( "Samples/Parallax/TextureCopy.cs.slang");

    mpMinmaxCopyCompute = ComputeProgramWrapper::create(getDevice());
    mpMinmaxCopyCompute->createProgram( "Samples/Parallax/Minmax.cs.slang", "copyFromScalarToVector");

    mpMinmaxMipmapCompute = ComputeProgramWrapper::create(getDevice());
    mpMinmaxMipmapCompute->createProgram( "Samples/Parallax/Minmax.cs.slang", "mipmapMinmax" );

    mpQuickConemapCompute = ComputeProgramWrapper::create(getDevice());
    mpQuickConemapCompute->createProgram("Samples/Parallax/QuickConemap.cs.slang", "main", {
        {kQuickGenAlgDefine, mQCMCompSettings.algorithm},
        {kMaxAtTexelCenterDefine, mQCMCompSettings.maxAtTexelCenter ? "1" : "0"} }
    );

    mpMaxCompute = ComputeProgramWrapper::create(getDevice());
    mpMaxCompute->createProgram("Samples/Parallax/MaxMip2d.cs.slang", "main_calcMaxMip");
    mpInitMaxMipCompute = ComputeProgramWrapper::create(getDevice());
    mpInitMaxMipCompute->createProgram("Samples/Parallax/MaxMips.cs.slang", "main_initMaxMip");

    mpCalcMaxMipCompute = ComputeProgramWrapper::create(getDevice());
    mpCalcMaxMipCompute->createProgram("Samples/Parallax/MaxMips.cs.slang", "main_calcMaxMip");
    // geometry
    initSquare(getDevice(), mpVertexBuffer, mpVao);

    mpParallaxRenderState->setVao(mpVao);
    mpDebugRenderState->setVao( mpVao );

    // camera
    resetCamera();

    // texture
    mGenerateMips = true;
    LoadAlbedoTexture();

    mGenerateMips = false;
    LoadHeightmapTexture();


    {
        Sampler::Desc desc;
        desc.setFilterMode(TextureFilteringMode::Linear, TextureFilteringMode::Linear, TextureFilteringMode::Linear);
        desc.setAddressingMode(TextureAddressingMode::Clamp, TextureAddressingMode::Clamp, TextureAddressingMode::Clamp);
        mpSampler = getDevice()->createSampler(desc);
    }
    {
        Sampler::Desc desc;
        desc.setFilterMode(TextureFilteringMode::Point, TextureFilteringMode::Point, TextureFilteringMode::Point);
        mpSamplerNearest = getDevice()->createSampler(desc);
    }

    mpParallaxVars->getRootVar()[ "gSampler" ] = mpSampler;
    mpDebugVars->getRootVar()["gSampler"] = mpSamplerNearest;

    // other
    // toggleVsync(true);


    //mRunHeightmapCompute = true;

    // generate a conemap in the first frame
    mRunMinmaxCompute = true;
    mRunQuickConemapCompute = true;
    mQCMCompSettings.algorithm = "2";
    mQCMCompSettings.maxAtTexelCenter = false;
    mQCMCompSettings.name = "Quick Conemap";

    //mRunMaxMipCompute = true;
    //mRunQDMCompute = true;
}

void Parallax::onFrameRender(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo)
{
    ShaderVar pParallaxVars = mpParallaxVars->getRootVar();
    ShaderVar pDebugVars = mpDebugVars->getRootVar();

    // save texture to file
    if (doSaveTexture == 1 || doSaveTexture == 2) {
        ref<Texture>& pTexToCopy = doSaveTexture == 1 ? mpHeightmapTex : mpConeTex;
        doSaveTexture = 0;
        if (pTexToCopy) {
            ref<Texture> pCopiedTexture = getDevice()->createTexture2D(pTexToCopy->getWidth(), pTexToCopy->getHeight(), ResourceFormat::RGBA32Float, 1, 1, nullptr, ResourceBindFlags::RenderTarget);
            pRenderContext->blit(pTexToCopy->getSRV(0, 1), pCopiedTexture->getRTV());
            pCopiedTexture->captureToFile(0, 0, saveFilePath, Bitmap::FileFormat::ExrFile);
        }
    }
    // procedural heightmap generation
    if (mRunHeightmapCompute) {
        mRunHeightmapCompute = false;
        ScopedProfilerEvent pe(pRenderContext, "compute_Heightmap");
        mpHeightmapTex = generateProceduralHeightmap(mProcHMCompSettings, pRenderContext);
        pParallaxVars["gTexture"] = mpHeightmapTex;
        float2 res = float2(mpHeightmapTex->getWidth(), mpHeightmapTex->getHeight());
        pParallaxVars["FScb"]["HMres"] = res;
        pParallaxVars["FScb"]["HMres_r"] = 1.f / res;
    }
    // conemap or relaxed conemap generation
    if (mRunConemapCompute) {
        mRunConemapCompute = false;
        ScopedProfilerEvent pe(pRenderContext, "compute_Conemap");
        mpConeTex = generateConemap(mCMCompSettings, mpHeightmapTex, pRenderContext);
        pParallaxVars["gTexture"] = mpConeTex;
        mpParallaxProgram->addDefine("DO_SQRT_LOOKUP", mCMCompSettings.DO_SQRT_LOOKUP ? "1" : "0");
    }
    // minmax mipmap for quick conemap generation
    if ( mRunMinmaxCompute )
    {
        mRunMinmaxCompute = false;
        ScopedProfilerEvent pe(pRenderContext, "compute_MinMaxMip");
        mpMinmaxTex = generateMinmaxMipmap(mpHeightmapTex, pRenderContext);
    }
    // quick conemap generation
    if (mRunQuickConemapCompute) {
        mRunQuickConemapCompute = false;
        ScopedProfilerEvent pe(pRenderContext, "compute_QuickConemap");
        mpConeTex = generateQuickConemap(mQCMCompSettings, mpMinmaxTex, pRenderContext);
        pParallaxVars["gTexture"] = mpConeTex;
    }
    if (mRunQDMCompute)
    {
        mRunQDMCompute = false;
        ScopedProfilerEvent pe(pRenderContext, "compute_QDM");
        mpQDMTex = generateQDMMap(mpHeightmapTex, pRenderContext);
        if (mpQDMTex)
        {
            pParallaxVars["gTexture"] = mpQDMTex;
        }
    }
    if (mRunMaxMipCompute)
    {
        mRunMaxMipCompute = false;
        ScopedProfilerEvent pe(pRenderContext, "compute_MaxMip");
        mpMaxMipTex = generateMaxMipMap(mpHeightmapTex, pRenderContext);
        if (mpMaxMipTex)
        {
            pParallaxVars["MaxMipTexture"] = mpMaxMipTex; // WARNING: Different texture
        }
    }

    // camera
    mCameraController.update();
    mpCamera->beginFrame();

    // clear bg
    const float4 clearColor( 1.0f, 1.0f, 1.0f, 1 );
    pRenderContext->clearFbo( pTargetFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All );

    float4x4 m = translate(float4x4::identity(), mRenderSettings.translate);
    m = rotate(m, mRenderSettings.angle, normalize(mRenderSettings.axis));
    m = scale(m, mRenderSettings.scale);

    // main draw
    if ( !mDebugSettings.drawDebug )
    {
        mpParallaxRenderState->setFbo( pTargetFbo );
        pParallaxVars[ "FScb" ][ "center" ] = float2( 0 );
        pParallaxVars[ "FScb" ][ "scale" ] = 1.f;
        pParallaxVars[ "FScb" ][ "lightDir" ] = mRenderSettings.lightDir;
        pParallaxVars[ "FScb" ][ "camPos" ] = mpCamera->getPosition();
        pParallaxVars[ "FScb" ][ "heightMapHeight" ] = mRenderSettings.heightMapHeight;
        pParallaxVars[ "FScb" ][ "discardFragments" ] = mRenderSettings.discardFragments;
        pParallaxVars[ "FScb" ][ "displayNonConverged" ] = mRenderSettings.displayNonConverged;
        pParallaxVars[ "FScb" ][ "lightIntensity" ] = mRenderSettings.lightIntensity;
        pParallaxVars[ "FScb" ][ "steps" ] = mRenderSettings.stepNum;
        pParallaxVars[ "FScb" ][ "refine_steps" ] = mRenderSettings.refineStepNum;
        pParallaxVars[ "FScb" ][ "relax" ] = mRenderSettings.relax;
        pParallaxVars[ "FScb" ][ "oneOverSteps" ] = 1.0f / mRenderSettings.stepNum;
        pParallaxVars[ "FScb" ][ "const_isolate" ] = 1;

        if (mRenderSettings.selectedParallaxFun == 10 && mpMaxMipTex)
            pParallaxVars["FScb"]["HMMaxMip"] = mpMaxMipTex->getDepth() - 1; // WARNING: Slides play the role of mips
        if (mRenderSettings.selectedParallaxFun == 11 && mpQDMTex)
            pParallaxVars["FScb"]["HMMaxMip"] = mpQDMTex->getMipCount() - 1;

        pParallaxVars[ "VScb" ][ "viewProj" ] = mpCamera->getViewProjMatrix();

        pParallaxVars[ "VScb" ][ "model" ] = m;
        pParallaxVars[ "VScb" ][ "modelIT" ] = inverse(transpose(m));

        mPixelDebug.beginFrame(pRenderContext, uint2(pTargetFbo->getWidth(), pTargetFbo->getHeight()));
        mPixelDebug.prepareProgram(mpParallaxProgram, pParallaxVars);
        pRenderContext->draw( mpParallaxRenderState.get(), mpParallaxVars.get(), kVertices.size(), 0 );
        mPixelDebug.endFrame(pRenderContext);
    }
    else
    {
        if (!mpDebugTex) return;

        mpDebugRenderState->setFbo( pTargetFbo );

        // debug stuff!
        pDebugVars["VScb"]["viewProj"] = mpCamera->getViewProjMatrix();
        pDebugVars[ "VScb" ][ "model" ] = m;
        pDebugVars[ "VScb" ][ "modelIT" ] = inverse(transpose(m));
        pDebugVars[ "FScb" ][ "currentLevel" ] = mDebugSettings.mipLevel;
        pDebugVars[ "FScb" ][ "channels" ] = mDebugSettings.debugChannels;
        const auto slices = mpDebugTex->getDepth();
        pDebugVars["FScb"]["slice3D"] = slices > 1 ? int2(mDebugSettings.slice, slices) : int2(-1);

        pDebugVars[ "gTexture" ].setSrv( mpDebugTex->getSRV() );
        if (slices > 1)
            pDebugVars["gTexture3D"].setSrv(mpDebugTex->getSRV());

        pRenderContext->draw( mpDebugRenderState.get(), mpDebugVars.get(), kVertices.size(), 0 );
    }

    mScreenCapture.captureIfRequested(pTargetFbo);
}

void Parallax::onShutdown()
{
}

bool Parallax::onKeyEvent(const KeyboardEvent& keyEvent)
{
    if(mCameraController.onKeyEvent(keyEvent)) return true;

    if (keyEvent.key == Input::Key::Escape) {
        return true; // don't let Falcor exit when pressing Escape
    }
    if (keyEvent.key == Input::Key::L) {
        mRenderSettings.lightDir = normalize(mpCamera->getTarget() - mpCamera->getPosition());
        return true;
    }
    if (keyEvent.key == Input::Key::C && keyEvent.type == KeyboardEvent::Type::KeyPressed)
    {
        if (keyEvent.hasModifier(Input::Modifier::Ctrl))
        {
            // copy model string to clipboard
            const auto& model = getModelAndSettingsString();
            ImGui::SetClipboardText(model.c_str());
            return true;
        }
        else
        {
            // create screen capture
            mScreenCapture.captrueNextFrame(getModelAndSettingsString());
            return true;
        }
    }
    const uint32_t keycode = (uint32_t)keyEvent.key;
    const uint32_t key0code = (uint32_t)Input::Key::Key0;
    const uint32_t key9code = (uint32_t)Input::Key::Key9;
    if (keycode >= key0code && keycode <= key9code) {
        uint32_t ind = keycode - key0code;
        if (keyEvent.hasModifier(Input::Modifier::Shift)) {
            if (ind < kRefinementFunList.size()) {
                mRenderSettings.selectedRefinementFun = kRefinementFunList[ind].value;
                mRenderSettings.setRefinementFun();
                return true;
            }
        }
        else
        {
            if (ind < kParallaxFunList.size()) {
                mRenderSettings.selectedParallaxFun = kParallaxFunList[ind].value;
                mRenderSettings.setParallaxFun();
                return true;
            }
        }
    }

    return false;
}

bool Parallax::onMouseEvent(const MouseEvent& mouseEvent)
{
    if (mPixelDebug.isEnabled() ) return mPixelDebug.onMouseEvent(mouseEvent);

    return mCameraController.onMouseEvent(mouseEvent);
}

bool Parallax::onGamepadState(const GamepadState& gamepadState)
{
    return mCameraController.onGamepadState(gamepadState);
}

void Parallax::onHotReload(HotReloadFlags reloaded)
{
}

void Parallax::onResize(uint32_t width, uint32_t height)
{
    mpCamera->setAspectRatio((float)width / (float)height);
}

void Parallax::resetCamera() {
    mpCamera->setPosition({-0.9f, 0.1f, -0.9f});
    mpCamera->setTarget({0.0f, -0.6f, 0.0f});
    mpCamera->setNearPlane(0.001f);
    mpCamera->beginFrame();
}

void Parallax::LoadHeightmapTexture()
{
    std::filesystem::path resolvedPath = AssetResolver::getDefaultResolver().resolvePath(mHeightmapName);
    mpHeightmapTex = Texture::createFromFile(getDevice(), resolvedPath, mGenerateMips, false);
    if (!mpHeightmapTex) {
        Falcor::msgBox("Heighmap texture error","Couldn't load heightmap texture", MsgBoxType::Ok, MsgBoxIcon::Error);
        return;
    }
    mpHeightmapTex->setName(filenameFromPath(mHeightmapName.string()));
    mpConeTex.reset();
    mpMinmaxTex.reset();
    mpQDMTex.reset();
    mpMaxMipTex.reset();
    ShaderVar pParallaxVars = mpParallaxVars->getRootVar();
    pParallaxVars["gTexture"] = mpHeightmapTex;
    float2 res = float2(mpHeightmapTex->getWidth(), mpHeightmapTex->getHeight());
    pParallaxVars["FScb"]["HMres"] = res;
    pParallaxVars["FScb"]["HMres_r"] = 1.f / res;
}

void Parallax::LoadAlbedoTexture()
{
    std::filesystem::path resolvedPath = AssetResolver::getDefaultResolver().resolvePath(mAlbedoName);
    mpAlbedoTex = Texture::createFromFile(getDevice(), resolvedPath, mGenerateMips, true);
    if (!mpAlbedoTex) {
        Falcor::msgBox("Albedo texture error", "Couldn't load albedo texture", MsgBoxType::Ok, MsgBoxIcon::Error);
        return;
    }
    mpAlbedoTex->setName(filenameFromPath(mAlbedoName.string()));
    //mpParallaxProgram->addDefine( "USE_ALBEDO_TEXTURE", "1" );
    getDevice()->getProgramManager()->addGlobalDefines({{"USE_ALBEDO_TEXTURE", "1"}});
    mpParallaxVars->getRootVar()[ "gAlbedoTexture" ] = mpAlbedoTex;
}

ref<Texture> Parallax::generateProceduralHeightmap(const ProceduralHeightmapComputeSettings& settings, RenderContext* pRenderContext) const
{
    if (!mpHeightmapCompute)
        return nullptr;
    auto& comp = *mpHeightmapCompute;

    auto w = settings.newHMapSize.x;
    auto h = settings.newHMapSize.y;
    ResourceFormat format = settings.newHmap16bit ? ResourceFormat::R16Unorm : ResourceFormat::R8Unorm;
    comp.getProgram()->addDefine(kHeightFunDefine, std::to_string(settings.heightFun));

    auto pTex = getDevice()->createTexture2D(w, h, format, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
    pTex->setName("Procedural Heightmap");

    comp["tex2D_uav"].setUav(pTex->getUAV(0));
    uint2 maxSize = { w, h };
    comp["CScb"]["maxSize"] = maxSize;
    comp["CScb"]["intParams"] = settings.HMapIntParams;
    comp["CScb"]["floatParams"] = settings.HMapFloatParams;
    comp.runProgram(w, h, 1);

    return pTex;
}

ref<Texture> Parallax::generateConemap(const ConemapComputeSettings& settings, const ref<Texture>& pHeightmap, RenderContext* pRenderContext) const
{
    if (!mpConemapCompute || !pHeightmap)
        return nullptr;
    auto& comp = *mpConemapCompute;
    auto w = pHeightmap->getWidth();
    auto h = pHeightmap->getHeight();
    ResourceFormat format = settings.newHmap16bit ? ResourceFormat::RG16Unorm : ResourceFormat::RG8Unorm;
    auto pTex = getDevice()->createTexture2D(w, h, format, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
    pTex->setName(settings.name);
    comp.getProgram()->addDefine(kConeTypeDefine, settings.algorithm);
    comp.getProgram()->addDefine("DO_SQRT_LOOKUP", settings.DO_SQRT_LOOKUP ? "1" : "0");

    comp["heightMap"].setSrv(pHeightmap->getSRV());
    comp["CScb"]["srcLevel"] = 0;
    comp["gSampler"] = mpSampler;
    comp["coneMap"].setUav(pTex->getUAV(0));
    uint2 maxSize = { w, h };
    comp["CScb"]["maxSize"] = maxSize;
    comp["CScb"]["oneOverMaxSize"] = 1.0f / float2(maxSize);
    comp["CScb"]["searchSteps"] = settings.relaxedConeSearchSteps;
    comp["CScb"]["oneOverSearchSteps"] = 1.0f / settings.relaxedConeSearchSteps;
    comp["CScb"]["bitCount"] = settings.newHmap16bit ? 65535 : 255;
    comp.runProgram(w, h, 1);

    if (settings.POSTPROCESS_MIN)
    {
        auto pTex2 = getDevice()->createTexture2D(
            w, h, format, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
        );
        pTex2->setName(settings.name);
        auto& comp2 = *mpConemapPostprocess;
        comp2.getProgram()->addDefine(kConeTypeDefine, settings.algorithm);
        comp2["coneMap_in"].setSrv(pTex->getSRV());
        comp2["CScb"]["srcLevel"] = 0;
        comp2["gSampler"] = mpSampler;
        comp2["coneMap"].setUav(pTex2->getUAV(0));
        comp2["CScb"]["maxSize"] = maxSize;
        comp2["CScb"]["oneOverMaxSize"] = 1.0f / float2(maxSize);
        comp2["CScb"]["searchSteps"] = settings.relaxedConeSearchSteps;
        comp2["CScb"]["oneOverSearchSteps"] = 1.0f / settings.relaxedConeSearchSteps;
        comp2["CScb"]["bitCount"] = settings.newHmap16bit ? 65535 : 255;
        comp2.runProgram(w, h, 1);

        pTex = pTex2;
    }

    return pTex;
}

ref<Texture> Parallax::generateMinmaxMipmap(const ref<Texture>& pHeightmap, RenderContext* pRenderContext) const
{
    // Initialize the minmax LOD 0
    uint2 maxSize = { pHeightmap->getWidth(), pHeightmap->getHeight() };
    auto pTex = getDevice()->createTexture2D(maxSize.x, maxSize.y, ResourceFormat::RG16Unorm, 1, uint32_t(-1), nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);

    auto& copyCS = *mpMinmaxCopyCompute;
    copyCS["CScb"]["maxSize"] = maxSize;
    copyCS["heightMap"].setSrv(pHeightmap->getSRV(0));
    copyCS["dstMinmaxMap"].setUav(pTex->getUAV(0));
    copyCS.runProgram(maxSize.x, maxSize.y, 1);

    // And do the minmax pooled mipmapping for all remaining LODs
    auto& minmaxCS = *mpMinmaxMipmapCompute;
    for (uint currentLevel = 0; currentLevel < pTex->getMipCount() - 1; ++currentLevel)
    {
        maxSize /= 2u;
        minmaxCS["CScb"]["maxSize"] = maxSize;
        minmaxCS["CScb"]["currentLevel"] = currentLevel;
        minmaxCS["srcMinmaxMap"].setSrv(pTex->getSRV());
        minmaxCS["dstMinmaxMap"].setUav(pTex->getUAV(currentLevel + 1));
        minmaxCS.runProgram(maxSize.x, maxSize.y, 1);
    }
    return pTex;
}



ref<Texture> Parallax::generateQuickConemap(const QuickConemapComputeSettings& settings, const ref<Texture>& pMinmaxMipmap, RenderContext* pRenderContext) const
{
    if (!mpQuickConemapCompute || !pMinmaxMipmap)
        return nullptr;
    auto& comp = *mpQuickConemapCompute;
    auto w = pMinmaxMipmap->getWidth();
    auto h = pMinmaxMipmap->getHeight();
    ResourceFormat coneMapFormat = settings.newHmap16bit ? ResourceFormat::RG16Unorm : ResourceFormat::RG8Unorm;
    auto pTex = getDevice()->createTexture2D(w, h, coneMapFormat, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
    pTex->setName(settings.name);
    comp.getProgram()->addDefine(kQuickGenAlgDefine, settings.algorithm);
    comp.getProgram()->addDefine(kMaxAtTexelCenterDefine, settings.maxAtTexelCenter ? "1" : "0");
    comp["srcMinmaxMap"].setSrv(pMinmaxMipmap->getSRV(0));
    comp["dstConeMap"].setUav(pTex->getUAV(0));
    uint2 maxSize = { w, h };
    comp["CScb"]["maxLevel"] = pMinmaxMipmap->getMipCount() - 2u;
    comp["CScb"]["maxSize"] = maxSize;
    comp["CScb"]["deltaHalf"] = 0.5f / float2(maxSize);
    comp.runProgram(w, h, 1);

    
    if (settings.POSTPROCESS_MIN)
    {
        auto pTex2 = getDevice()->createTexture2D(
            w, h, coneMapFormat, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
        );
        pTex2->setName(settings.name);
        auto& comp2 = *mpConemapPostprocess;
        comp2.getProgram()->addDefine(kConeTypeDefine, settings.algorithm);
        comp2["coneMap_in"].setSrv(pTex->getSRV());
        comp2["CScb"]["srcLevel"] = 0;
        comp2["coneMap"].setUav(pTex2->getUAV(0));
        comp2["CScb"]["maxSize"] = maxSize;
        // the rest of the settings are unused
        comp2["gSampler"] = mpSampler;
        comp2["CScb"]["oneOverMaxSize"] = 1.0f / float2(maxSize);
        comp2["CScb"]["searchSteps"] = 2; 
        comp2["CScb"]["oneOverSearchSteps"] = 0.5f;
        comp2["CScb"]["bitCount"] = settings.newHmap16bit ? 65535 : 255;

        comp2.runProgram(w, h, 1);

        pTex = pTex2;
    }
    return pTex;
}
ref<Texture> Parallax::generateQDMMap(const ref<Texture>& pHeightmap, RenderContext* pRenderContext) const
{
    if (!mpTextureCopyCompute || !mpMaxCompute || !pHeightmap)
        return nullptr;
    auto w = pHeightmap->getWidth();
    auto h = pHeightmap->getHeight();
    auto format = Falcor::getNumChannelBits(pHeightmap->getFormat(), 0) == 8 ? ResourceFormat::R8Unorm : ResourceFormat::R16Unorm;
    auto pQDMTex = getDevice()->createTexture2D(
        w,
        h,
        format,
        1,
        Falcor::Resource::kMaxPossible,
        nullptr,
        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
    );

    pQDMTex->setName("QDM Max Mipmaped");

    // Initialize the MIP 0 with the heightmap
    auto& copy = *mpTextureCopyCompute;
    copy.getProgram()->addDefine("ASSIGN", "DST.x = SRC.x;");
    uint2 texSize = uint2(w, h);
    copy["CScb"]["maxSize"] = texSize;
    copy["src"].setSrv(pHeightmap->getSRV(0));
    copy["dst"].setUav(pQDMTex->getUAV(0));
    copy.runProgram( texSize.x, texSize.y);

    auto& prog = *mpMaxCompute;

    for (uint currentLevel = 0; currentLevel < pQDMTex->getMipCount() - 1; ++currentLevel)
    {
        prog["CScb"]["inputSize"] = texSize;
        texSize = max(uint2(1u), texSize / 2u);
        prog["CScb"]["outputSize"] = texSize;
        prog["inputTexture"].setSrv(pQDMTex->getSRV(currentLevel));
        prog["outputTexture"].setUav(pQDMTex->getUAV(currentLevel + 1));
        prog.runProgram( texSize.x, texSize.y);
    }
    return pQDMTex;
}
ref<Texture> Parallax::generateMaxMipMap(const ref<Texture>& pHeightmap, RenderContext* pRenderContext) const
{

    if ( !mpInitMaxMipCompute || !pHeightmap) return nullptr;

    auto w = pHeightmap->getWidth();
    auto h = pHeightmap->getHeight();
    uint32_t d = static_cast<uint32_t>(Falcor::math::log2<float>(w)) + 2;
    auto format = Falcor::getNumChannelBits(pHeightmap->getFormat(), 0) == 8 ? ResourceFormat::R8Unorm : ResourceFormat::R16Unorm;
    auto pMaxMipTex = getDevice()->createTexture3D(
        w,
        h,
        d,
        format,
        1,
        nullptr,
        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
    );

    pMaxMipTex->setName("Max Mipmaped");

    
    uint2 texSize = uint2(w, h);

    auto& init = *mpInitMaxMipCompute;

    init["CScb"]["inputSize"] = texSize;
    init["CScb"]["outputSize"] = texSize;
    init["inputTexture"].setSrv(pHeightmap->getSRV(0));
    init["outputTexture"].setUav(pMaxMipTex->getUAV(0));

    init.runProgram(texSize.x, texSize.y);

    auto& calc = *mpCalcMaxMipCompute;
    for (uint l = 2; l < d; ++l)
    {
        texSize.x >>= 1;
        texSize.y >>= 1;

        calc["CScb"]["outputSize"] = texSize;
        calc["CScb"]["outputLevel"] = l;
        calc["outputTexture"].setUav(pMaxMipTex->getUAV(0));
        calc.runProgram(texSize.x, texSize.y);
    }


    return pMaxMipTex;
}



std::string Parallax::getModelAndSettingsString()
{


    std::stringstream ss;
    ss << "render_";
    if (mRenderSettings.selectedParallaxFun == 2)
    {
        if (!mpHeightmapTex)
            return "noHeightMap";
        ss << "Heightmap"
           << "_res-" << mpHeightmapTex->getWidth()
           << "_primary-linear-" << mRenderSettings.stepNum;
    }
    else if (mRenderSettings.selectedParallaxFun == 3)
    {
        if (!mpConeTex)
            return "noConeMap";
        ss << mpConeTex->getName()
           << "_res-" << mpConeTex->getWidth()
           << "_primary-conestep-"<< mRenderSettings.stepNum;
        if (mRenderSettings.CONSERVATIVE_STEP)
            ss << "-conservative";
    }

    ss << "_refine-"
       << (mRenderSettings.selectedRefinementFun == 0   ? "none"
           : mRenderSettings.selectedRefinementFun == 1 ? "linear"
                                                        : "binary-");
    if (mRenderSettings.selectedParallaxFun == 2)
        ss << mRenderSettings.refineStepNum;

    if (!mRenderSettings.displayNonConverged)
        ss << "_hide-unconverged";

    return ss.str();
}

inline void Parallax::ScreenCapture::captrueNextFrame(std::string fileName, std::string directory)
{
    mDoCapture = true;
    mFileName = fileName;
    mDirectory = directory;
}

inline void Parallax::ScreenCapture::captureIfRequested(const ref<Fbo>& pTargetFbo)
{
    if (mDoCapture)
    {
        mDoCapture = false;
        const std::string& f = mFileName == "" ? getExecutableName() : mFileName;
        std::filesystem::path d = mDirectory == "" ? getRuntimeDirectory() : mDirectory;
        std::filesystem::path path = findAvailableFilename(f, d, "png");
        std::cout << path << std::endl;
        pTargetFbo->getColorTexture(0).get()->captureToFile(0, 0, path);
    }
}


















int runMain(int argc, char** argv)
{
    SampleAppConfig config;
    config.windowDesc.title = "Parallax Techniques";
    config.windowDesc.resizableWindow = true;
    config.windowDesc.mode = Window::WindowMode::Normal; //Window::WindowMode::Fullscreen;
    Parallax project(config);

    return project.run();
}

int main(int argc, char** argv)
{
    return catchAndReportAllExceptions([&]() { return runMain(argc, argv); });
}

#include "ParallaxPixelDebug.h"
#include "imgui.h"
#include "implot.h"
#include <sstream>
#include "Utils/Math/ScalarMath.h"
#include "Core/Program/Program.h"
#include "Core/Program/ShaderVar.h"

namespace Falcor
{
ParallaxPixelDebug::ParallaxPixelDebug(ref<Device> pDevice, uint32_t printCapacity, uint32_t assertCapacity)
    : PixelDebug(pDevice, printCapacity, assertCapacity)
{
    ImPlot::CreateContext();
}

ParallaxPixelDebug::~ParallaxPixelDebug()
{
    ImPlot::DestroyContext();
}

static void ImGuiDecodedPrintRecord(const std::pair<std::string, PrintRecord>& pr)
{
    if (ImGui::BeginTable(pr.first.c_str(), 1, ImGuiTableFlags_Borders | ImGuiTableFlags_NoHostExtendX))
    {
        ImGui::TableSetupColumn(pr.first.c_str());
        ImGui::TableHeadersRow();

        const PrintRecord& v = pr.second;

        for (uint32_t i = 0; i < v.count; i++)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            uint32_t bits = v.data[i];
            switch ((PrintValueType)v.type)
            {
            case PrintValueType::Bool:
                ImGui::Text((bits != 0 ? "true" : "false"));
                break;
            case PrintValueType::Int:
                ImGui::Text("%d", (int32_t)bits);
                break;
            case PrintValueType::Uint:
                ImGui::Text("%u",bits);
                break;
            case PrintValueType::Float:
                ImGui::Text("%.7f",fstd::bit_cast<float>(bits));
                break;
            default:
                ImGui::Text("INVALID VALUE");
                break;
            }
        }
        ImGui::EndTable();
    }
}

void ParallaxPixelDebug::renderUI(Gui::Widgets* widget)
{
    FALCOR_CHECK(!mRunning, "Logging is running, call endFrame() before renderUI().");

    if (!widget) return;

    // Configure logging.
    widget->checkbox("Pixel debug", mEnabled);
    widget->tooltip(
        "Enables shader debugging.\n\n"
        "Left-mouse click on a pixel to select it.\n"
        "Use print(value) or print(msg, value) in the shader to print values for the selected pixel.\n"
        "All basic types such as int, float2, etc. are supported.\n"
        "Use assert(condition) or assert(condition, msg) in the shader to test a condition.",
        true
    );
    if (mEnabled)
    {
        widget->var("Selected pixel", mSelectedPixel);
    }

    // Fetch stats and show log if available.
    bool isNewData = copyDataToCPU();
    if (mDataValid)
    {
        ImGui::BeginChild(
            "PixelDebugLog",
            ImGui::GetContentRegionAvail(),
            false,
            ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar);

        std::vector<std::pair<std::string, PrintRecord>> DecodedPrintData;
        mHeightValues.clear();
        mHeightSamples.clear();
        mCellBoundaryParams.clear();
        float2 u = float2(0.0, 0.0), u2 = float2(0.0, 0.0);

        bool foundGroundThruth = false;

        float groundTruthParam = 1.0;

        for (auto v : mPrintData)
        {
            auto it = mHashToString.find(v.msgHash);
            if (it != mHashToString.end() && !it->second.empty())
            {
                if (it->second == "PARALLAX_PIXEL_DEBUG_TEXTURE_DIMS")
                {
                    mTextureDims = uint3(v.data[0], v.data[1], v.data[2]);
                }
                else if ( it->second == "PARALLAX_PIXEL_DEBUG_TEXTURE_SAMPLE" )
                {
                    mHeightSamples.push_back(fstd::bit_cast<float>(v.data[0]));
                    mHeightSamples.push_back(fstd::bit_cast<float>(v.data[1]));
                    mHeightSamples.push_back(fstd::bit_cast<float>(v.data[2]));
                    mHeightSamples.push_back(fstd::bit_cast<float>(v.data[3]));
                }
                else if ( it->second == "PARALLAX_PIXEL_DEBUG_GTH_BEGIN" )
                {
                    DecodedPrintData.push_back({"Start UV", {0, v.type, 2, 0, uint4(v.data[0], v.data[1],0,0)}});
                    DecodedPrintData.push_back({"End UV", {0, v.type, 2, 0, uint4(v.data[2], v.data[3], 0, 0)}});
                    u= float2(fstd::bit_cast<float>(v.data[0]), fstd::bit_cast<float>(v.data[1]));
                    u2 = float2(fstd::bit_cast<float>(v.data[2]), fstd::bit_cast<float>(v.data[3]));
                }
                else if ( it->second == "PARALLAX_PIXEL_DEBUG_GTH_HEIGHT" )
                {
                    mHeightValues.push_back(fstd::bit_cast<float>(v.data[0]));
                    mHeightValues.push_back(fstd::bit_cast<float>(v.data[1]));
                    mHeightValues.push_back(fstd::bit_cast<float>(v.data[2]));
                    mHeightValues.push_back(fstd::bit_cast<float>(v.data[3]));
                }
                else if ( it->second == "PARALLAX_PIXEL_DEBUG_GTH_END" )
                {
                    DecodedPrintData.push_back({"Ground thruth param", v});
                    groundTruthParam = fstd::bit_cast<float>(v.data[0]);
                    foundGroundThruth = true;
                    float2 texCoordHitPos = math::lerp(u, u2, groundTruthParam) * float2(mTextureDims.xy());
                    DecodedPrintData.push_back(
                        {"Hit Pos texcoord",
                         PrintRecord{0,
                          (math::uint)Falcor::PrintValueType::Float,
                          2,
                          0,
                          uint4(
                              fstd::bit_cast<math::uint>(texCoordHitPos.x),
                              fstd::bit_cast<math::uint>(texCoordHitPos.y),
                              0, 0
                          )}
                        }
                    );
                }
                else
                {
                    DecodedPrintData.push_back({it->second, v});
                }
            }
        }


        for (const auto& pr : DecodedPrintData)
        {
            ImGuiDecodedPrintRecord(pr);
        }

        std::ostringstream oss;
        // Print list of asserts.
        if (!mAssertData.empty())
        {
            oss << "\n";
            for (auto v : mAssertData)
            {
                oss << "Assert at (" << v.launchIndex.x << ", " << v.launchIndex.y << ", " << v.launchIndex.z << ")";
                auto it = mHashToString.find(v.msgHash);
                if (it != mHashToString.end() && !it->second.empty())
                    oss << " " << it->second;
                oss << "\n";
            }

            if (widget)
                widget->text(oss.str());
        }


        static bool separateWidget = false;


        ImGui::Checkbox("Separate widget", &separateWidget);

        auto drawTrace = [&]()
        {
            if (length(u - u2) > 1e-6)
            {
                ImGui::SliderInt("Mipmap level", &mSelectedMipLevel, 0, mTextureDims.z - 1);

                if (mHeightValues.size() > 1)
                {
                    float2 prevSamplePos = float2(u.x * (mTextureDims.x >> mSelectedMipLevel), u.y * (mTextureDims.y >> mSelectedMipLevel));
                    float paramStep = 1.0f / (mHeightValues.size() - 1);
                    for (int i = 1; i < mHeightValues.size(); ++i)
                    {
                        float p = float(i) * paramStep;
                        float2 u_p = lerp(u, u2, p);
                        float2 samplePos =
                            float2(u_p.x * (mTextureDims.x >> mSelectedMipLevel), u_p.y * (mTextureDims.y >> mSelectedMipLevel));

                        if (floorf(samplePos.x) != floorf(prevSamplePos.x))
                        {
                            float t = (floorf(samplePos.x) - prevSamplePos.x) / (samplePos.x - prevSamplePos.x);

                            mCellBoundaryParams.push_back(p - (1.0f - t) * paramStep);
                        }
                        if (floorf(samplePos.y) != floorf(prevSamplePos.y))
                        {
                            float t = (floorf(samplePos.y) - prevSamplePos.y) / (samplePos.y - prevSamplePos.y);

                            mCellBoundaryParams.push_back(p - (1.0f - t) * paramStep);
                        }
                        prevSamplePos = samplePos;
                    }
                }

                if (ImPlot::BeginPlot("Heights" , ImVec2(-1, (separateWidget ? -1:0)), ImPlotFlags_CanvasOnly))
                {
                    ImPlot::SetupAxes("t", "height", ImPlotAxisFlags_NoGridLines, ImPlotAxisFlags_NoGridLines);
                    ImPlot::SetupAxesLimits(0, 1.0, 0, 1.0);
                    ImPlot::PlotShaded("Heights", mHeightValues.data(), mHeightValues.size(), 0.0, 1.0f / (mHeightValues.size() - 1));
                    ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
                    //ImPlot::PlotShaded("Samples", mHeightSamples.data(), mHeightSamples.size(), 0.0, 1.0f / (mHeightSamples.size() - 1));
                    ImPlot::PlotStairs(
                        "Samples",
                        mHeightSamples.data(),
                        mHeightSamples.size(),
                        1.0f / (mHeightSamples.size() - 1),
                        0.0,
                        ImPlotStairsFlags_Shaded
                    );
                    ImPlot::PopStyleVar();
                    ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.15f);
                    static const float ray_x[2] = {0.0, 1.0};
                    static const float ray_y[2] = {1.0, 0.0};
                    ImPlot::PlotLine("Ray", ray_x, ray_y, 2);
                    ImPlot::PopStyleVar();

                    //if (!mCellBoundaryParams.empty())
                    //{
                    //    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 0.5f);
                    //    ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.15f);
                    //    ImPlot::PlotInfLines("Cell boundary", mCellBoundaryParams.data(), mCellBoundaryParams.size());
                    //    ImPlot::PopStyleVar();
                    //}

                    if (foundGroundThruth)
                        ImPlot::PlotInfLines("ground truth", &groundTruthParam, 1);
                    ImPlot::EndPlot();
                }
            }
        };

        if ( !separateWidget )
        {
            drawTrace();
        }
        
        ImGui::EndChild();

        if ( separateWidget )
        {
            ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
            ImGui::Begin("Trace");
            drawTrace();
            ImGui::End();
        }
    }
}

void ParallaxPixelDebug::prepareProgram(const ref<Program>& pProgram, const ShaderVar& var)
{
    PixelDebug::prepareProgram(pProgram, var);
    if (mEnabled)
    {
        ShaderVar ParallaxPixelDebugParams = var["ParallaxPixelDebugCB"];
        ParallaxPixelDebugParams["level"]=mSelectedMipLevel;
    }
}
} // namespace Falcor

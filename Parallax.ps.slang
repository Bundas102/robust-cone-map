#ifndef PARALLAX_PS_INCLUDED
#define PARALLAX_PS_INCLUDED

//#define USE_DEBUG_COLOR
void setDebugColor(float3 col);
void getDebugColor(inout float3 col);
#ifdef USE_DEBUG_COLOR
static float3 debug_color = float3(-1);
void setDebugColor(float3 col){debug_color = col;}
void getDebugColor(inout float3 col)
{
    if(debug_color.x != -1)
        col = debug_color;
}
#else
void setDebugColor(float3 col)
{
}
void getDebugColor(inout float3 col)
{
}
#endif

cbuffer FScb : register(b0)
{
    float3 lightDir;
    float3 camPos;
    float2 center;
    float2 HMres;
    float2 HMres_r;
    uint HMMaxMip;
    float scale;
    float heightMapHeight;
    float relax;
    uint steps;
    uint refine_steps;
    float oneOverSteps;
    float lightIntensity;
    bool discardFragments;
    bool displayNonConverged;
    int const_isolate;
};

Texture2D gTexture;
Texture3D<float2> MaxMipTexture;
Texture2D gAlbedoTexture;
SamplerState gSampler;

struct FsIn
{
    float4 posH : SV_POSITION;
    float2 texC : TEXCOORD;
    float3 posW : POSITION;
    float3 norm : NORMAL;
};


float getH_texture(float2 uv)
{
    return gTexture.SampleLevel(gSampler, uv, 0).r;
}


float getH(float2 uv)
{
#if defined(PARALLAX_FUN) && PARALLAX_FUN == 10
    return MaxMipTexture.SampleLevel(gSampler, float3(uv+0.5*HMres_r, 0), 0).x;
#else
    return getH_texture(uv);
#endif
}
struct BilinearWeights
{
    float2 weights;
    float2 center_uv;
    int2 texel_index;
};
BilinearWeights getBilinearWeights(float2 uv, float2 texSize, float2 oneOverTexSize)
{
    BilinearWeights ret;
    float2 texelCoordF = uv * texSize - 0.5;
    float2 topLeft = floor(texelCoordF);
    ret.weights = texelCoordF - topLeft;
    ret.center_uv = (topLeft + 1.0) * oneOverTexSize;
    ret.texel_index = (int2) topLeft;
    return ret;
}
float2 getHC_texture(float2 uv)
{
#if BILINEAR_BY_HAND
    BilinearWeights ww = getBilinearWeights(uv, HMres, HMres_r);
    float2 r = ww.weights;
    float2 c = ww.center_uv;
    float2 t00, t01, t11, t10;
    float4 gR = gTexture.GatherRed(gSampler, c, int2(0, 0));
    t00.x = gR.w;
    t01.x = gR.x;
    t11.x = gR.y;
    t10.x = gR.z;
    float4 gG = gTexture.GatherGreen(gSampler, c, int2(0, 0));
    t00.y = gG.w;
    t01.y = gG.x;
    t11.y = gG.y;
    t10.y = gG.z;

    float2 t0_ = lerp(t00, t01, r.y);
    float2 t1_ = lerp(t10, t11, r.y);
    float2 t__ = lerp(t0_, t1_, r.x);

#if DO_SQRT_LOOKUP
    // Conemap aperture square root is stored
    t__.y *= t__.y;
#endif
    
    return t__;
    
#else
    
    float2 data = gTexture.SampleLevel(gSampler, uv,0).rg;
#if DO_SQRT_LOOKUP
    // Conemap aperture square root is stored
    data.y *= data.y;
#endif    
    return data;

#endif
}

float3 getNormalTBN_finiteDiff(float2 uv)
{
    const float mutliplier = 1;
    float2 delta = HMres_r * mutliplier;
    float2 one_over_delta = HMres / mutliplier;
    
    float2 du = float2(delta.x, 0);
    float2 dv = float2(0, delta.y);
    float dhdu = 0.5 * one_over_delta.x * (getH(uv + du) - getH(uv - du));
    float dhdv = 0.5 * one_over_delta.y * (getH(uv + dv) - getH(uv - dv));
    return normalize(float3(-dhdu, -dhdv, 1));
}

float3 getNormalTBN(float2 uv)
{
    return getNormalTBN_finiteDiff(uv);
}


#include "ParallaxPixelDebug/ParallaxPixelDebug.slangh"

#include "FindIntersection.slang"
#include "Refinement.slang"

// inverts a 3x3 matrix
// input: the 3 columns of the matrix
float3x3 invMat(float3 a, float3 b, float3 c)
{
    float3 r1 = cross(b, c);
    float3 r2 = cross(c, a);
    float3 r3 = cross(a, b);
    float invDet = 1 / dot(r3, c);
    return invDet * float3x3(r1, r2, r3);
}

float4 main(FsIn fs) : SV_TARGET
{
    float2 u = fs.texC; // original texcoords
    float3 p = fs.posW; // fragment world pos
    float3 col = float3(.5);

    // calc tangent space
    float2 dxu = ddx_fine(u);
    float2 dyu = ddy_fine(u);
    float3 dxp = ddx_fine(p);
    float3 dyp = ddy_fine(p);
    
    float2x3 P = float2x3(dxp, dyp);
    float2x2 U = float2x2(dxu, dyu);
    float2x2 U_inv = float2x2(U[1][1], -U[0][1], -U[1][0], U[0][0]) / determinant(U);
    float2x3 A = mul(U_inv, P);
    float3 T = A[0];
    float3 B = A[1];
    float3 N = normalize(fs.norm) * heightMapHeight;

    float3x3 TBN = float3x3(T, B, N);
    float3x3 TBN_inv = transpose(invMat(T, B, N));

    float3 viewDirW = p - camPos;
    float3 viewDirT = mul(viewDirW, TBN_inv);

    // texcoords where the camera ray intersects the bottom plate
    float2 u2 = u - viewDirT.xy / viewDirT.z;
    
    ParallaxPixelDebug_Init(uint2(fs.posH.xy), u, u2);

    // find the intersection with the height map
    HMapIntersection I = findIntersection(u, u2);
    float2 u3 = refineIntersection(I, u, u2);

    // intersection is outside of the bottom plate
    if (any(u3 < 0) || any(u3 > 1))
    {
        if (discardFragments)
            discard;
        return float4(1, 0, 0, 1);
    }
    
    // get normal in TBN space
    float3 normT = getNormalTBN(u3);
    // transform normal from TBN to world coordinates
    float3 norm = normalize(mul(normT, transpose(TBN_inv)));
    

    // fetch the final albedo color
#if defined( USE_ALBEDO_TEXTURE ) && USE_ALBEDO_TEXTURE
    float3 albedo = gAlbedoTexture.Sample(gSampler, u3).rgb;
    col *= albedo;
#endif
    

    
    float diffuse = lightIntensity * saturate(dot(-normalize(lightDir), norm));
    col.rgb *= diffuse;

    if (displayNonConverged && !I.wasHit)
    {
        col.rgb = float3(1,0,1);
    }

    getDebugColor(col.rgb);
    return float4(col, 1);
}
#endif

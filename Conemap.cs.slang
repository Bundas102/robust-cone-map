cbuffer CScb : register(b0)
{
    uint2 maxSize;
    float2 oneOverMaxSize;
    uint searchSteps;
    float oneOverSearchSteps;
    int srcLevel;
    uint bitCount; // texture bit count per channel - needed for conservative quantization
};

Texture2D<float> heightMap;
Texture2D<float2> coneMap_in; // for postprocess
RWTexture2D<float2> coneMap; // [height, cone alpha]
SamplerState gSampler : register(s0);

float getH(float2 uv)
{
    return heightMap.SampleLevel(gSampler, uv, srcLevel).r;
}

float2 texCoord(uint2 texelInd)
{
    return ((float2) texelInd + 0.5) * oneOverMaxSize;
}

float dot2(float2 a)
{
    return dot(a, a);
}

float getConservativeCone(float baseHeight, float2 baseTexCoord, uint2 texelInd)
{
    float2 t = texCoord(texelInd);
    float d2 = dot2(baseTexCoord - t);
    float deltaH = heightMap.Load(int3(texelInd, srcLevel)) - baseHeight;

    return deltaH <= 0 ? 1.0 : sqrt(d2) / deltaH;
}

// Adapted from https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-18-relaxed-cone-stepping-relief-mapping
float getRelaxedCone(float baseHeight, float2 baseTexCoord, uint2 texelInd, float minRatio)
{
    float2 t = texCoord(texelInd);
    
    float3 src = float3(baseTexCoord, 1+0.001);
    float3 dst = float3(t, heightMap.Load(int3(texelInd, srcLevel)));

    if ((dst.z <= baseHeight) || length(dst.xy - baseTexCoord) > minRatio * (dst.z - baseHeight))
        return 1;
    
    float3 vec = dst - src; // Ray direction
    vec /= -vec.z; // Scale ray direction so that vec.z = -1.0
    vec *= dst.z; // Scale again
    float3 step_fwd = vec * oneOverSearchSteps; // Length of a forward step
    // Search until a new point outside the surface
    float3 ray_pos = dst + step_fwd;
    for (uint i = 1; i < searchSteps; i++)
    {
        float current_height = getH(ray_pos.xy);
        if (current_height >= ray_pos.z)
        {
            ray_pos += step_fwd;
        }
        else
        {
            break;
        }
    }
    // Original texel depth
    float src_texel_height = baseHeight;
    // Compute the cone ratio
    float cone_ratio = (ray_pos.z <= src_texel_height) ? 1.0 :
        length(ray_pos.xy - baseTexCoord) / (ray_pos.z - src_texel_height);
    return cone_ratio;
}


float getCone(float baseHeight, float2 baseTexCoord, uint2 texelInd, float minRatio)
{
#ifndef CONE_TYPE
    #error "CONE_TYPE is undefined"
    return 0;
#elif CONE_TYPE == 1
    return getConservativeCone(baseHeight, baseTexCoord, texelInd);
#elif CONE_TYPE == 2
    return getRelaxedCone(baseHeight, baseTexCoord, texelInd, minRatio);
#elif CONE_TYPE == 4
    return 0; // this shouldn't be called, instead call main_new_fallingEdge from main
#else
    #error "Unknown CONE_TYPE"
    return 0;
#endif
}

void main_new_fallingEdge();

// Utility function to write out the cone data taking into account the output format
void WriteConeMap(uint2 id, float baseH, float minTan)
{
    // Dummer stored aperture square roots, most likely due to the underlying sRGB
    // format of the relief texture. We use linear color space, however, bilinear
    // filtering of sqrt values us different from bilin of linear values
#if DO_SQRT_LOOKUP
    minTan = sqrt(max(0.0, minTan));
#endif
    
    // truncate to the target bit width so that we don't round up to incorrectly large cones
    float truncatedMinTan = uint(max(0.0, minTan) * bitCount) / float(bitCount);
    coneMap[id] = float2(baseH, truncatedMinTan);
}

[numthreads(16, 16, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
#if CONE_TYPE == 4
    main_new_fallingEdge(threadId);
    return;
#else
    if (any(threadId.xy >= maxSize))
        return;
    float2 baseT = texCoord(threadId.xy); // texture coords
    float baseH = heightMap.Load(int3(threadId.xy, srcLevel));

    //float maxCos = 0;
    float minTan = 1;
    uint w = maxSize.x;
    uint h = maxSize.y;
    
    for (uint i = 0; i < w; ++i)
    {
        for (uint j = 0; j < h; ++j)
        {
            uint2 id = uint2(i, j);
            if (any(threadId.xy != id))
            {
                minTan = min(minTan, getCone(baseH, baseT, id, minTan));

            }
        }
    }
    WriteConeMap(threadId.xy, baseH, minTan);
#endif
}


// ckeck a cell for monotone drop
// baseH, baseT, baseIJ: the cone apex
// dstIJ:                the point to check
// dir:                  the direction of the cell from dstIJ
// inout minRatio:       the tan value to update
void updateMinTan(float baseH, float2 baseT, uint2 baseIJ, uint2 dstIJ, int2 dir, inout float minRatio)
{
    // T: texture coodinates, H: heightmap value
    const float2 dstT = texCoord(dstIJ); // dst: the position to check for a potential falling edge
    const float2 deltaT = dstT - baseT;
    const float dist = length(deltaT); // distance of texture coordinates
    
    // the cone is already over the max height
    if (dist >= minRatio * (1 - baseH))
        return;
    
    const float h00 = heightMap.Load(int3(dstIJ, srcLevel));
    const float deltaH = h00 - baseH;

    // the checked point is under the cone
    // tan >= minTanSoFar      where  tan = dist/deltaH
    if (dist >= minRatio * deltaH)
        return;
    
    const float h10 = heightMap.Load(int3(dstIJ + int2(dir.x, 0), srcLevel));
    const float h01 = heightMap.Load(int3(dstIJ + int2(0, dir.y), srcLevel));
    const float h11 = heightMap.Load(int3(dstIJ + dir, srcLevel));
    
    const bool isLimitingVertex = h00 > h10 || h00 > h01 || h10 > h11 || h01 > h11;
    
    if(!isLimitingVertex)
        return;

    const float cone_ratio = dist / deltaH;
    minRatio = min(minRatio, cone_ratio);
}

[numthreads(16, 16, 1)]
void main_new_fallingEdge(uint3 threadId : SV_DispatchThreadID)
{
    if (any(threadId.xy >= maxSize))
        return;
    const float2 baseT = texCoord(threadId.xy); // texture coords
    const uint2 baseIJ = threadId.xy;
    const float baseH = heightMap.Load(int3(threadId.xy, srcLevel));
    
    const uint w = maxSize.x;
    const uint h = maxSize.y;
    const uint startBand = 1;
    const uint endBand = max(w, h);

    const int2 dirs[4] = { { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
    
    float minTan = 1;

    for (uint r = startBand; r <= endBand; r++)
    {
        // early out when the cone is already too narrow
        // this assumes a square texture
        if (r * oneOverMaxSize.x >= minTan * (1 - baseH))
            break;

        for (uint dir_id = 0; dir_id < 4; ++dir_id)
        {
            const int2 dd = dirs[dir_id];
            const int2 minIJ = int2(dd.x > 0 ? 0 : 1, dd.y > 0 ? 0 : 1);
            const int2 maxIJ = int2(dd.x > 0 ? w - 2 : w - 1, dd.y > 0 ? h - 2 : h - 1);
            
            int i = baseIJ.x + dd.x * r;
            int j = baseIJ.y;
            if (i >= minIJ.x && i <= maxIJ.x)
            {
                for (uint k = 0; k <= r && (j >= minIJ.y && j <= maxIJ.y); k++, j += dd.y)
                {
                    updateMinTan(baseH, baseT, baseIJ, uint2(i, j), dd, minTan);
                }
            }
            i = baseIJ.x;
            j = baseIJ.y + dd.y * r;
            if (j >= minIJ.y && j <= maxIJ.y)
            {
                for (uint k = 0; k < r && (i >= minIJ.x && i <= maxIJ.x); k++, i += dd.x)
                {
                    updateMinTan(baseH, baseT, baseIJ, uint2(i, j), dd, minTan);
                }
            }
        }
    }
    WriteConeMap(threadId.xy, baseH, minTan);
}


[numthreads(16, 16, 1)]
void main_postprocess_max(uint3 threadId : SV_DispatchThreadID)
{
    if (any(threadId.xy >= maxSize))
        return;
    const uint w = maxSize.x;
    const uint h = maxSize.y;
    const uint2 baseIJ = threadId.xy;
    float2 texel_val = coneMap_in.Load(int3(baseIJ, srcLevel)).rg;
    
    for (int i = -1; i <= 1; ++i)
    {
        for (int j = -1; j <= 1; ++j)
        {
            int2 n_IJ = baseIJ + int2(i, j);
            n_IJ.x = clamp(n_IJ.x, 0, w - 1);
            n_IJ.y = clamp(n_IJ.y, 0, h - 1);
            const float n_val = coneMap_in.Load(int3(n_IJ, srcLevel)).g;
            texel_val.y = min(texel_val.y, n_val);
        }
    }

    coneMap[threadId.xy] = texel_val;
}

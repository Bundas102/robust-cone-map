Texture2D<float> inputTexture;
RWTexture2D<float> outputTexture;

cbuffer CScb : register(b0)
{
    uint2 inputSize;
    uint2 outputSize;
};


uint2 inputAddressRepeat(uint2 id)
{
    return id % inputSize;
}
uint2 inputAddressClamp(uint2 id)
{
    return min(id, inputSize - 1);
}
uint2 inputAddress(uint2 id)
{
#if defined( CLAMP_INPUT_ADDRESS )
    return inputAddressClamp(id);
#elif defined( REPEAT_INPUT_ADDRESS )
    return inputAddressRepeat(id);
#else
    return id;
#endif
}
uint2 inputAddress(uint x, uint y)
{
    return inputAddress(uint2(x, y));
}


// Calculates the next mip level (maximum of the underlying texels)
[numthreads(16, 16, 1)]
void main_calcMaxMip(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= outputSize))
        return;
    
    float v00 = inputTexture[inputAddress(2 * id.x + 0, 2 * id.y + 0)];
    float v10 = inputTexture[inputAddress(2 * id.x + 1, 2 * id.y + 0)];
    float v01 = inputTexture[inputAddress(2 * id.x + 0, 2 * id.y + 1)];
    float v11 = inputTexture[inputAddress(2 * id.x + 1, 2 * id.y + 1)];

    float maxV = max(max(v00, v10), max(v01, v11));

    outputTexture[id.xy] = maxV;
}

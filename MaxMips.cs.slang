


cbuffer CScb
{
    uint2 inputSize;
    uint2 outputSize;
    uint outputLevel;
};

uint2 inputAddressClamp(uint2 id,uint2 size)
{
    return min(id, size - 1);
}

Texture2D<float> inputTexture;
RWTexture3D<float> outputTexture;

[numthreads(16, 16, 1)]
void main_initMaxMip(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= outputSize) || id.z > 0)
        return;

    float q00 = inputTexture[id.xy];
    float q10 = inputTexture[inputAddressClamp(uint2(id.x+1, id.y),inputSize)];
    float q01 = inputTexture[inputAddressClamp(uint2(id.x, id.y+1),inputSize)];
    float q11 = inputTexture[inputAddressClamp(uint2(id.x+1, id.y+1), inputSize)];

    outputTexture[uint3(id.xy,0)] = q00;
    outputTexture[uint3(id.xy,1)] = max(max(q00,q01),max(q10,q11));
}

[numthreads(16, 16, 1)]
void main_calcMaxMip(uint3 id : SV_DispatchThreadID)
{
    
    if (any(id.xy >= outputSize)|| id.z > 0) return;

    float q00 = outputTexture[uint3(2 * id.xy,           outputLevel-1)];
    float q10 = outputTexture[uint3(2 * id.xy+uint2(1,0),outputLevel-1)];
    float q01 = outputTexture[uint3(2 * id.xy+uint2(0,1),outputLevel-1)];
    float q11 = outputTexture[uint3(2 * id.xy+uint2(1,1),outputLevel-1)];
    
    outputTexture[uint3(id.xy,outputLevel)] = max(max(q00,q01),max(q10,q11));
}


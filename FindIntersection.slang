#ifndef FINDINTERSECTION_INCLUDED
#define FINDINTERSECTION_INCLUDED

#include "ParallaxPixelDebug/ParallaxPixelDebug.slangh"

import IntersectBilinearPatch;

struct HMapIntersection
{
    float2 uv;
    float t; // uv = (1-t)*u + t*u2
    float last_t;
    bool wasHit;
};

static const HMapIntersection INIT_INTERSECTION = { 0.0.xx, 0.0, 0.0, false };

HMapIntersection findIntersection_bumpMapping(float2 u, float2 u2)
{
    HMapIntersection ret = INIT_INTERSECTION;
    ret.uv = u2;
    ret.t = 1;
    ret.last_t = 1;
    return ret;
}

HMapIntersection findIntersection_parallaxMapping(float2 u, float2 u2)
{
    float t = 1 - getH(u2);
    HMapIntersection ret = INIT_INTERSECTION;
    ret.uv = (1 - t) * u + t * u2;
    ret.t = t;
    ret.last_t = t;
    return ret;
}

HMapIntersection findIntersection_linearSearch(float2 u, float2 u2)
{
    HMapIntersection ret = INIT_INTERSECTION;
    float dt = oneOverSteps;
    float t = 1;
    float2 du = (u2 - u) * dt;
    float2 uu = u;
    //TraceAnalyserRegisterValue(0, 0, float4(uu, t, 0.0));
    for (uint i = 0; i < steps; ++i)
    {
        //TraceAnalyserRegisterValue(i, 0, float4(uu, t, 0.0));
        t -= dt;
        uu += du;
        float h = getH(uu);
        if (h >= t)
        {
            ret.wasHit = true;
            //TraceAnalyserRegisterValue(i+1, 0, float4(uu, t, 0.0));
            break;
        }
    }

    ret.uv = uu;
    ret.t = 1 - t;
    ret.last_t = 1 - t - dt;
    return ret;
}


// Adapted from "Cone Step Mapping: An Iterative Ray-Heightfield Intersection Algorithm" - Jonathan Dummer
HMapIntersection findIntersection_coneStepMapping(float2 u, float2 u2)
{
    float3 ds = float3(u2 - u, 1);
    ds = normalize(ds);
    float w = 1 / HMres.x;
    float iz = sqrt(1.0 - ds.z * ds.z); // = length(ds.xy)
    float sc = 0;
    float2 t = getHC_texture(u);
    int stepCount = 0;
    float zTimesSc = 0.0;

    float2 dirSign = float2(ds.x < 0 ? -1 : 1, ds.y < 0 ? -1 : 1) * 0.5 / HMres.xy;

    
    while (1.0 - ds.z * sc > t.x && stepCount < steps)
    {
        zTimesSc = ds.z * sc;
#if CONSERVATIVE_STEP
        const float2 p = u + ds.xy * sc;
        const float2 cellCenter = (floor(p*HMres.xy - .5) + 1) / HMres.xy;
        const float2 wall = cellCenter + dirSign;
        const float2 stepToCellBorder = (wall - p) / ds.xy;
        w = min(stepToCellBorder.x, stepToCellBorder.y) + 1e-5;
#endif
        sc += relax * max(w, (1.0 - zTimesSc - t.x) * t.y / (t.y * ds.z + iz));
        t = getHC_texture(u + ds.xy * sc);
        ++stepCount;
    }
    
    HMapIntersection ret = INIT_INTERSECTION;
    ret.last_t = zTimesSc;
    ret.wasHit = (stepCount < steps);
    float tt = ds.z * sc;
    ret.uv = (1 - tt) * u + tt * u2;
    ret.t = tt;
    return ret;
}




// Quadtree Displacement Mapping with Height Blending
// GPU Pro 1

HMapIntersection findIntersection_QDM(float2 u, float2 u2)
{
    
    float3 v = float3(u2 - u, -1.0);

    bool2 flip = v.xy < 0.0;
    
    float2 r = lerp(u, 1.0 - u, float2(flip));

    v.xy = lerp(v.xy, -v.xy, float2(flip));
    
    float2 invV = float2(
        v.x == 0.0 ? 1e16 : rcp(v.x),
        v.y == 0.0 ? 1e16 : rcp(v.y)
    );

    
    float CurrentHeight = 1.0;
    uint2 NodeId = uint2(0);
    uint NodeCount = 1;
    
    int Level = (int) HMMaxMip;
    int iter = 0;

    while (Level >= 0 && iter < steps)
    {
        print("nc",uint4(NodeId, NodeCount, Level));
        uint2 SampleId = uint2(
            flip.x ? NodeCount - 1 - NodeId.x : NodeId.x,
            flip.y ? NodeCount - 1 - NodeId.y : NodeId.y);
        
        float d = gTexture.Load(int3( (int2)SampleId, Level)).r;
        print("heights", float4(r,CurrentHeight, d));

        bool needDescending = true;
        if ( d < CurrentHeight)
        {
            float3 t_cell = float3((1.0 - r) * invV, (CurrentHeight - d) * NodeCount);
            float t = min(t_cell.x, min(t_cell.y, t_cell.z));
            int at = dot(int2(1, 2), int2(t_cell.yz == t));

            r += t * v.xy;

            if (at <2)
            {
                CurrentHeight += t * v.z;

                //TraceAnalyserRegisterValue(iter, 3, float4((NodeId + r) / float(NodeCount), 0.0,0.0));
                // Moving to next cell, depending where v points.
                NodeId += uint2(at == 0 ? 1 : 0, at == 1 ? 1 : 0);
                r = float2(at == 0 ? 0.0 : r.x, at == 1 ? 0.0 : r.y);

                // accending on levels
                needDescending = false;
                r = 0.5 * (r + float2(NodeId % 2 == 1));
                NodeCount >>= 1;
                NodeId >>= 1;
                v[2] *= 2.0;
                Level += 1;
            }
            else
            {
                CurrentHeight = d;
            }

        }
        
        if (needDescending)
        {
            // descending on levels
            NodeCount <<= 1;
            float2 new_r = r * 2.0;
            NodeId = NodeId * 2 + uint2(new_r >= 1.0);
            r = new_r - float2(new_r >= 1.0);
            v.z *= 0.5;
            Level--;
        }
        
        iter++;
    }

    float2 CurrentUV = lerp(u2, u, CurrentHeight);
    float LastHeight = CurrentHeight + HMres_r.x;


    iter = 0;

    while (gTexture.SampleLevel(gSampler, CurrentUV, Level + 1).r < CurrentHeight && iter < steps)
    {
        LastHeight = CurrentHeight;

        float2 t_cell = (1.0 - r) * invV;
        float t = min(t_cell.x, t_cell.y);
        int at = t == t_cell.y ? 1 : 0;
        
        CurrentHeight += t * v.z;
        NodeId += uint2(at == 0 ? 1 : 0, at == 1 ? 1 : 0);
        r += t * v.xy - float2(at == 0, at == 1);

        CurrentUV = lerp(u2, u, CurrentHeight);
        iter++;
    }
    
    HMapIntersection ret = INIT_INTERSECTION;
    ret.t = 1.0 - CurrentHeight;
    ret.uv = CurrentUV;
    ret.last_t = 1.0 - LastHeight;
    ret.wasHit = Level < 0;

    return ret;
}




// Tevs et al. "Maximum Mipmaps for Fast, Accurate, and Scalable Dynamic Height Field Rendering", February 2008,DOI: 10.1145/1342250.1342279

HMapIntersection findIntersection_MaxMip(float2 u, float2 u2)
{
    
    float3 v = float3(u2 - u, -1.0);

    // We use a little trick to avoid the need for a branch in the loop.
    // If the direction is negative we flip the direction and the UV coordinates.
    // In this way we do not need to handle the case where the direction is negative during box/ray intersection.
    bool2 flip = v.xy < 0.0;
    print("flip",uint2(flip));
    float3 r = float3( flip.x ? 1.0 - u.x : u.x, flip.y ? 1.0 - u.y : u.y, 1.0);
    
    v.xy = lerp(v.xy, -v.xy, float2(flip));
    //print("v",v);
    float2 invV = float2(
        v.x == 0.0 ? 1e16 : rcp(v.x),
        v.y == 0.0 ? 1e16 : rcp(v.y)
        );
        
    print("u/v",r);
        
    uint2 NodeId = uint2(0);
    uint NodeCount = 1;
    int Level = (int)HMMaxMip;
    int iter = 0;
    
    HMapIntersection ret = INIT_INTERSECTION;

    while ( iter < steps )
    {
        print("iter",iter);
        uint2 SampleId = uint2(
            flip.x ? NodeCount - 1 - NodeId.x : NodeId.x,
            flip.y ? NodeCount - 1 - NodeId.y : NodeId.y);
        
        float height = MaxMipTexture.Load(int4( (int2)SampleId, max(Level,1),0)).r;
        print("samplePos",int3(SampleId,Level));
        print("NC",uint2(NodeCount,1<<(HMMaxMip-max(Level,1))));
        print("id",NodeId);
        print("height,rayH",float2(height,r.z));
        bool rayAboveHeightField = r.z > height;
        print("r",float4(r,1.0-r.z));
        print("rayAbove",rayAboveHeightField);

        if ( Level == 0 && !rayAboveHeightField)
        {
            print("Hit Pos cell",SampleId);
            float3 q00 = float3( float2(SampleId),MaxMipTexture.Load(int4(SampleId,0,0)).r);
            float3 q10 = float3( float2(SampleId+uint2(1,0)),MaxMipTexture.Load(int4(SampleId+uint2(1,0),0,0)).r);
            float3 q01 = float3( float2(SampleId+uint2(0,1)),MaxMipTexture.Load(int4(SampleId+uint2(0,1),0,0)).r);
            float3 q11 = float3( float2(SampleId+uint2(1,1)),MaxMipTexture.Load(int4(SampleId+uint2(1,1),0,0)).r);

            print("q00",float3(q00));
            print("q01",float3(q01));
            print("q10",float3(q10));
            print("q11",float3(q11));

            print("cellMax",MaxMipTexture.Load(int4(SampleId,1,0)).r);

            float t;
            if ( intersectBilinearPatch(t, q00, q01, q10, q11, float3(u*HMres,1.0),float3(u2*HMres,0.0)) )
            {
                r.z=1.0-t;
                break;
            }
        }
        
        if (rayAboveHeightField || Level==0 )
        {
            // intersect with the upcoming cell boundary
            float3 t_cell = float3(((NodeId + 1) * rcp(NodeCount) -r.xy ) * invV, r.z - height);
            print("t_cell",t_cell);
            if (t_cell.z <= 0.0 ) t_cell.z = 1e16;
            float t = min(t_cell.x, min(t_cell.y, t_cell.z));
            
            // move to intersection point
            r += t * v;

            // step over the cell boundary, if needed
            uint t_cell_armin = 0;
            if (t_cell.y == t) t_cell_armin = 1;
            if (t_cell.z == t) t_cell_armin = 2;

            NodeId += uint2(t_cell_armin == 0 ? 1 : 0, t_cell_armin == 1 ? 1 : 0);

            print("t argmin",t_cell_armin);    
            print("dahh",float4(r,1.0-r.z));    
            print("dahhdah",float2(r.xy)*NodeCount);  
            // ascend one mipmap level, if we can
            if (Level > 0 && all(NodeId % 2 == 0 ))
            {
                NodeCount >>= 1;
                NodeId = NodeId / 2;
                Level++;
            }
            print("dahh2",uint3(NodeId,Level));
        }
        else
        {
            // descend one mipmap level
            if (Level > 1)
            {
                NodeCount <<= 1;
                NodeId = NodeId * 2;
                float2 cell_center = (NodeId + uint2(1)) * rcp(NodeCount);
                if ( r.x >= cell_center.x) NodeId.x++;
                if ( r.y >= cell_center.y) NodeId.y++;
                print("cell center", cell_center);
                print("cell center ifbigger", uint2(r.xy >= cell_center));
            }
            Level--;
        }
        
        iter++;
    }

    ret.t = 1.0 - r.z;
    ret.uv = lerp(u,u2, ret.t);
    ret.wasHit = iter < steps;
    
    return ret;
}



// u: frontPlate tex coords, u2 back plate tex coords 
HMapIntersection findIntersection(float2 u, float2 u2)
{
#ifndef PARALLAX_FUN
    #error "PARALLAX_FUN is not defined"
    HMapIntersection r;
    return r;
#elif PARALLAX_FUN == 0
    return findIntersection_bumpMapping(u, u2);
#elif PARALLAX_FUN == 1
    return findIntersection_parallaxMapping(u, u2);
#elif PARALLAX_FUN == 2
    return findIntersection_linearSearch(u, u2);
#elif PARALLAX_FUN == 3
    return findIntersection_coneStepMapping(u, u2);
#elif PARALLAX_FUN == 10
    return findIntersection_MaxMip(u, u2);
#elif PARALLAX_FUN == 11
    return findIntersection_QDM(u, u2);
#else
    #error "PARALLAX_FUN has an unused value"
    HMapIntersection r; return r;
#endif
}
#endif

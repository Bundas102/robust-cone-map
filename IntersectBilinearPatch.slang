import Utils.Debug.PixelDebug;

float copySign(const float x, const float y)
{
    return abs(x) * sign(y);
}

// Code from Alexander Reshetov : "Cool Patches: A Geometric Approach to Ray/Bilinear Patch Intersections" RTGems
bool intersectBilinearPatch(out float t, float3 q00,float3 q01,float3 q10,float3 q11,float3 rayOrigin, float3 rayTarget)
{
    t = 1.0/0.0;                                   // need solution for the smallest t > 0
    float3 rayDir = rayTarget - rayOrigin;

    float3 e10 = q10 - q00;                     // q01---------------q11
    float3 e11 = q11 - q10;                     // |                   |
    float3 e00 = q01 - q00;                     // | e00           e11 |  we precompute
    float3 qn = cross(q10 - q00, q01 - q11);    // |        e10        |  qn = cross(q10-q00,q01-q11)
    q00 -= rayOrigin;                           // q00---------------q10
    q10 -= rayOrigin;
    float a = dot(cross(q00, rayDir), e00);     // the equation is
    float c = dot(qn, rayDir);                  // a + b u + c u^2
    float b = dot(cross(q10, rayDir), e11);     // first compute a+b+c

    b -= a + c;                                 // and then b
    float det = b * b - 4.f * a * c;

    print("abcd",float4(a,b,c,det));

    if (det < 0.f)
    {
        return false;                                 // see the right part of Figure 8.5
    }

    det = sqrt(det);                            // we -use_fast_math in CUDA_NVRTC_OPTIONS

    float u1, u2,u,v;                               // two roots (u parameter)

    if (c == 0.f)
    {                                           // if c == 0, it is a trapezoid
        u1 = -a / b; u2 = -1.f;                 // and there is only one root
    }
    else
    {                                           // (c != 0 in Stanford models)
        u1 = (-b - copySign(det, b)) / 2.f;     // numerically "stable" root
        u2 = a / u1;                            // Viete's formula for u1*u2
        u1 /= c;
    }
    print("u1 u2",float2(u1,u2));
    if (0.f <= u1 && u1 <= 1.f)
    {                                           // is it inside the patch?
        float3 pa = lerp(q00, q10, u1);         // point on edge e10 (Figure 8.4)
        float3 pb = lerp(e00, e11, u1);         // it is, actually, pb - pa
        float3 n = cross(rayDir, pb);
        det = dot(n, n);
        n = cross(n, pa);
        float t1 = dot(n, pb) / det;
        float v1 = dot(n, rayDir) / det;              // no need to check t1 < t
        print("t1 v1",float3(t1,v1,det));
        if (0.f <= v1 && v1 <= 1.0)
        {                                       // if t1 > ray.tmax it will be rejected in rtPotentialIntersection
            t = t1;
            u=u1;
            v=v1;
        }
    }
    if (0.f <= u2 && u2 <= 1.f)
    {                                           // it is slightly different,
        float3 pa = lerp(q00, q10, u2);         // since u1 might be good
        float3 pb = lerp(e00, e11, u2);         // and we need 0 < t2 < t1
        float3 n = cross(rayDir, pb);
        det = dot(n, n);
        n = cross(n, pa);
        float t2 = dot(n, pb) / det;
        float v2 = dot(n, rayDir) / det;
        print("t2 v2",float3(t2,v2,det));
        if (0.f <= v2 && v2 <= 1.0)
        {
            if (t > t2)
            {
                t = t2;
                u=u2;
                v=v2;
            }
        }
    }

    print("inters final",float3(t,u,v));
    return 0.f <= t && t <= 1.f;
}
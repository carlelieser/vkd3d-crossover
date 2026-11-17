RWTexture2D<float4> u0 : register(u0);
RWByteAddressBuffer o : register(u1);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float4 v = u0[uint2(0, 0)];                  /* typed UAV load */
    o.Store4(0, asuint(v));
    u0[uint2(0, 0)] = v + float4(0.25, 0, 0, 0); /* read-modify-write */
    u0[uint2(1, 0)] = float4(0.5, 0.25, 0.75, 1); /* plain store */
}

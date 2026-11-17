// Busy loop whose trip count comes from a root constant (2^32-1): far longer than Metal's GPU watchdog.
cbuffer C : register(b0) { uint count; uint seed; };
RWByteAddressBuffer u0 : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint acc = seed + id.x;
    for (uint i = 0; i < count; i++)
        for (uint j = 0; j < count; j++)
            acc = acc * 1664525u + 1013904223u;
    u0.Store(id.x * 4, acc);
}

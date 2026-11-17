// Writes through a root UAV; the probe binds an unmapped GPU virtual address, so this page-faults.
RWByteAddressBuffer u0 : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    u0.Store(id.x * 4096, id.x);
}

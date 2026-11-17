// NaN/Inf semantics check. Inputs come from a buffer so nothing constant-folds.
// in[0] = NaN, in[1] = +Inf, in[2] = 1.5, in[3] = -2.0
RWByteAddressBuffer io : register(u0);
[numthreads(1, 1, 1)]
void main()
{
    float nan = asfloat(io.Load(0)), inf = asfloat(io.Load(4)), a = asfloat(io.Load(8)), b = asfloat(io.Load(12));
    uint o = 64;
    io.Store(o + 0,  isnan(nan) ? 1u : 0u);                 // 1
    io.Store(o + 4,  isnan(a) ? 1u : 0u);                   // 0
    io.Store(o + 8,  asuint(isnan(nan) ? 0.0f : nan));      // 0 (scrub with isnan)
    io.Store(o + 12, asuint(max(nan, 0.0f)));               // 0 (D3D: max with one NaN returns the other)
    io.Store(o + 16, asuint(min(nan, 1.0f)));               // 1.0
    io.Store(o + 20, asuint(saturate(nan)));                // 0 (D3D: saturate(NaN) = 0)
    io.Store(o + 24, isfinite(inf) ? 1u : 0u);              // 0
    io.Store(o + 28, isinf(inf) ? 1u : 0u);                 // 1
    io.Store(o + 32, (nan == nan) ? 1u : 0u);               // 0
    io.Store(o + 36, (nan != nan) ? 1u : 0u);               // 1
    io.Store(o + 40, asuint(clamp(nan, 0.0f, 1.0f)));       // 0 (max then min)
    io.Store(o + 44, asuint(a / (b + 2.0f)));               // +Inf (1.5 / 0)
    io.Store(o + 48, asuint(max(a / (b + 2.0f), 0.0f) > 1e30f ? 1.0f : 0.0f)); // 1
    io.Store(o + 52, asuint(nan * 0.0f));                   // NaN (0x7fc00000-ish), not 0
    io.Store(o + 56, asuint(lerp(1.0f, 2.0f, saturate(nan)))); // 1.0
}

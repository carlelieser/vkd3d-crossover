import Metal
let src = """
#include <metal_stdlib>
using namespace metal;
// Game's loop: up to 128 tries of { old = s; CAS(s, old, old + 1); if (orig == old) break; }
kernel void buggy(device uint* out [[buffer(0)]], uint t [[thread_index_in_threadgroup]]) {
    threadgroup uint s[1]; if (t == 0) s[0] = 0; threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint i = 0; i < 128; i++) {
        uint r;
        do { r = s[0]; } while (!atomic_compare_exchange_weak_explicit((threadgroup atomic_uint*)&s[0], &r, s[0] + 1u, memory_order_relaxed, memory_order_relaxed) && r == s[0]);
        if (r == s[0]) break;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup); if (t == 0) out[0] = s[0];
}
kernel void fixed(device uint* out [[buffer(0)]], uint t [[thread_index_in_threadgroup]]) {
    threadgroup uint s[1]; if (t == 0) s[0] = 0; threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint i = 0; i < 128; i++) {
        uint old = s[0]; uint r;
        do { r = old; } while (!atomic_compare_exchange_weak_explicit((threadgroup atomic_uint*)&s[0], &r, old + 1u, memory_order_relaxed, memory_order_relaxed) && r == old);
        if (r == old) break;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup); if (t == 0) out[0] = s[0];
}
"""
let dev = MTLCreateSystemDefaultDevice()!
let lib = try! dev.makeLibrary(source: src, options: nil)
let q = dev.makeCommandQueue()!
for name in ["buggy", "fixed"] {
    let p = try! dev.makeComputePipelineState(function: lib.makeFunction(name: name)!)
    let b = dev.makeBuffer(length: 4, options: .storageModeShared)!
    let cb = q.makeCommandBuffer()!; let e = cb.makeComputeCommandEncoder()!
    e.setComputePipelineState(p); e.setBuffer(b, offset: 0, index: 0)
    e.dispatchThreadgroups(MTLSize(width: 1, height: 1, depth: 1), threadsPerThreadgroup: MTLSize(width: 64, height: 1, depth: 1))
    e.endEncoding(); cb.commit(); cb.waitUntilCompleted()
    print(name, "64 threads each add 1 -> total", b.contents().load(as: UInt32.self))
}

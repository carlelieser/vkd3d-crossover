struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O main(uint id : SV_VertexID) { O o; float2 uv = float2((id << 1) & 2, id & 2); o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = uv; return o; }

Texture2D<float4> t0 : register(t0);
cbuffer C : register(b0) { float4 k; };
float4 vs(uint id : SV_VertexID) : SV_Position { float2 uv = float2((id << 1) & 2, id & 2); return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }
float4 ps(float4 p : SV_Position) : SV_Target { return t0.Load(int3(0, 0, 0)) + k; }

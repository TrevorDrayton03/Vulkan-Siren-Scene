//TODO: multiply output.position with MVP
//TODO: color and textures after rendering a solid color

struct VertexInput
{
    [[vk::location(0)]]
    float3 position : POSITION;
    
    [[vk::location(1)]]
    float3 normal : NORMAL;
    
    [[vk::location(2)]]
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
};

[shader("vertex)]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 1.0);
    return output;
}

[shader("fragment)]
float4 fragmentMain() : SV_Target
{
    return float4(0.8, 0.6, 0.2, 1.0);
}
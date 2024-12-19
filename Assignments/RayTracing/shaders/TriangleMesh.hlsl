struct VertexShaderOutput
{
    float4 clipSpacePosition : SV_POSITION;
    float3 viewSpacePosition : POSITION;
    float3 viewSpaceNormal : NORMAL;
    float3 viewSpaceTangent : TANGENT;
    float3 viewSpaceBitangent : BITANGENT;
    float2 texCoord : TEXCOORD;
};

/// <summary>
/// Constants that can change every frame.
/// </summary>
cbuffer PerFrameConstants : register(b0)
{
    float4x4 projectionMatrix;
    float3 lightPosition;
    uint1 flags;
}

/// <summary>
/// Constants that can change per Mesh/Drawcall.
/// </summary>
cbuffer PerMeshConstants : register(b1)
{
    float4x4 modelViewMatrix;
}

/// <summary>
/// Constants that are really constant for the entire scene.
/// </summary>
cbuffer Material : register(b2)
{
    float4 ambientColor;
    float4 diffuseColor;
    float4 specularColorAndExponent;
}

Texture2D<float4> g_textureAmbient : register(t0);
Texture2D<float4> g_textureDiffuse : register(t1);
Texture2D<float4> g_textureSpecular : register(t2);
Texture2D<float4> g_textureEmissive : register(t3);
Texture2D<float4> g_textureNormal : register(t4);

SamplerState g_sampler : register(s0);

RaytracingAccelerationStructure TLAS : register(t5, space0); // Acceleration structure

struct MyCustomAttrIntersectionAttributes
{
    float4 baseColor;
    float3 normal;
};

VertexShaderOutput VS_main(float3 position : POSITION, float3 normal : NORMAL, float3 tangent : TANGENT, float2 texCoord : TEXCOORD)
{
    VertexShaderOutput output;

    float4 p4 = mul(modelViewMatrix, float4(position, 1.0f));
    output.viewSpacePosition = p4.xyz;
    output.viewSpaceNormal = mul(modelViewMatrix, float4(normal, 0.0f)).xyz;
    output.clipSpacePosition = mul(projectionMatrix, p4);
    output.texCoord = texCoord;

    output.viewSpaceTangent = mul((float3x3) modelViewMatrix, tangent);
    output.viewSpaceBitangent = cross(output.viewSpaceNormal, output.viewSpaceTangent);
    return output;
}

float4 PS_main(VertexShaderOutput input)
    : SV_TARGET
{
    // Compute light direction and distance
    float3 lightDir = normalize(lightPosition - input.clipSpacePosition.xyz);
    float lightDistance = length(lightPosition - input.clipSpacePosition.xyz);
    
    RayDesc ray;
    ray.Origin = input.viewSpacePosition.xyz;
    ray.Direction = normalize(lightDir);
    ray.TMin = 0.001;
    ray.TMax = lightDistance;
    
    RayQuery < RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES > q;
    q.TraceRayInline(TLAS, 0, 0xFF, ray);
    float3 finalColor = float3(0.0, 0.0, 0.0); // Initialize output color
    bool hit = false;
    
    while (q.Proceed())
    {

    }
    
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
            // A triangle was hit
        hit = true;
        q.CommitNonOpaqueTriangleHit();
    }

    // Shade the pixel based on ray result
    if (hit)
    {
        // In shadow: darker color
        finalColor = float4(0.2, 0.2, 0.2, 1.0); // Example: dark gray
    }
    else
    {
        // Not in shadow: normal color
        float3 normalColor = float4(1.0, 0.0, 0.0, 1.0); // Example: use normal as color
        finalColor = float4(normalColor, 1.0);
    }

    return float4(finalColor.xyz, 1.0f);
  
    
    //while (q.Proceed())
    //{
    //    if (q.CandidateType() == CANDIDATE_PROCEDURAL_PRIMITIVE)
    //    {
    //        float tHit;
    //        MyCustomAttrIntersectionAttributes candidateAttribs;

    //        //// Assume intersection attributes are calculated here
    //        //if (q.CandidateProceduralPrimitiveNonOpaque() &&
    //        //    MyProceduralIntersectionLogic(tHit, candidateAttribs, ray.Origin, ray.Direction, ))
    //        //{
    //        //    q.CommitProceduralPrimitiveHit(tHit);
    //        //    committedAttribs = candidateAttribs;
    //        //}
    //    }
    //}

    //// Determine shading based on the committed hit
    //switch (q.CommittedStatus())
    //{
    //    case COMMITTED_PROCEDURAL_PRIMITIVE_HIT: // hit shading
    //        finalColor = GetProcedualPrimitiveHitColor(
    //        committedAttribs,
    //        q.CommittedInstanceIndex(),
    //        q.CommittedPrimitiveIndex(),
    //        q.CommittedGeometryIndex(),
    //        q.CommittedRayT());
    //        break;

    //    case COMMITTED_NOTHING: // miss shading
    //        //finalColor = GetMissColor(ray);
    //        break;
    //}
}

bool MyProceduralIntersectionLogic(
    out float tHit, // Output: Distance to hit
    out MyCustomAttrIntersectionAttributes attribs, // Output: Intersection attributes
    float3 rayOrigin, // Ray origin
    float3 rayDirection, // Ray direction
    float3 sphereCenter, // Sphere center
    float sphereRadius)                           // Sphere radius
{
    // Vector from ray origin to sphere center
    float3 oc = rayOrigin - sphereCenter;

    // Coefficients of the quadratic equation
    float a = dot(rayDirection, rayDirection);
    float b = 2.0 * dot(rayDirection, oc);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;

    // Discriminant
    float discriminant = b * b - 4.0 * a * c;

    // No intersection if discriminant is negative
    if (discriminant < 0.0)
        return false;

    // Compute the two possible intersection distances
    float sqrtDiscriminant = sqrt(discriminant);
    float t0 = (-b - sqrtDiscriminant) / (2.0 * a);
    float t1 = (-b + sqrtDiscriminant) / (2.0 * a);

    // Find the nearest positive intersection
    tHit = (t0 > 0.0) ? t0 : t1;

    // If both intersections are negative, there's no hit
    if (tHit < 0.0)
        return false;

    // Compute intersection point and normal
    float3 hitPoint = rayOrigin + tHit * rayDirection;
    float3 normal = normalize(hitPoint - sphereCenter);

    // Set output attributes
    attribs.baseColor = float4(1.0, 0.5, 0.0, 1.0); // Example: Base color (orange)
    attribs.normal = normal; // Surface normal

    return true; // Intersection occurred
}


float3 GetMissColor(RayDesc rayDesc)
{
    // Gradient sky color
    float t = 0.5 * (rayDesc.Direction.y + 1.0); // Interpolate based on ray's Y direction
    float3 topColor = float3(0.5, 0.7, 1.0); // Sky blue
    float3 bottomColor = float3(1.0, 1.0, 1.0); // White
    return lerp(bottomColor, topColor, t);
}

float3 GetProcedualPrimitiveHitColor(
    MyCustomAttrIntersectionAttributes attribs, // Intersection attributes
    uint instanceIndex,
    uint primitiveIndex,
    uint geometryIndex,
    float rayT)
{
    // Directional light setup
    float3 lightDir = normalize(float3(-0.5, -1.0, -0.5)); // Directional light
    float3 lightColor = float3(1.0, 1.0, 1.0); // White light

    // Surface normal and base color from intersection attributes
    float3 normal = normalize(attribs.normal); // Surface normal
    float3 baseColor = attribs.baseColor.rgb; // Base color of sphere

    // Simple Lambertian diffuse lighting
    float diffuse = max(dot(normal, -lightDir), 0.0); // Diffuse intensity
    float3 shadedColor = baseColor * diffuse * lightColor; // Apply lighting

    return shadedColor;
}
// Camera ray generation shared by every entry point. Must match CameraFrameBlock in VulkanRenderer.cpp, std430 and std140 layout: four float4 at 0, 16, 32 and 48
module Camera;

public struct CameraFrameBlock
{
	public float4 Origin;
	public float4 Forward;
	public float4 RightScaled; // Right * aspect * tan(fov / 2)
	public float4 UpScaled; // Up * tan(fov / 2)
};

public struct Ray
{
	public float3 Origin;
	public float3 Direction;
};

// The same expression as Engine::GenerateCameraRay: row 0 at the top so +y is up. The subpixel offset is in [0, 1) across the pixel, (0.5, 0.5) is the centre
public Ray GenerateCameraRay(CameraFrameBlock camera, uint2 pixel, uint2 extent, float2 subpixel)
{
	const float l_X = 2.0 * (float(pixel.x) + subpixel.x) / float(extent.x) - 1.0;
	const float l_Y = 1.0 - 2.0 * (float(pixel.y) + subpixel.y) / float(extent.y);

	Ray l_Ray;
	l_Ray.Origin = camera.Origin.xyz;
	l_Ray.Direction = normalize(camera.Forward.xyz + l_X * camera.RightScaled.xyz + l_Y * camera.UpScaled.xyz);

	return l_Ray;
}

// Pixel centre, the diagnostic views and the CPU picking ray use this one
public Ray GenerateCameraRay(CameraFrameBlock camera, uint2 pixel, uint2 extent)
{
	return GenerateCameraRay(camera, pixel, extent, float2(0.5, 0.5));
}
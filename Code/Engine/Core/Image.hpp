#pragma once
#include "Engine/Core/Rgba8.hpp"
#include "Engine/Math/IntVec2.hpp"

#include <string>
#include <vector>

class Image
{
	friend class Renderer;
	friend class DX11Renderer;
	friend class DX12Renderer;
	friend class VulkanRenderer;

public:
	Image();
	Image(char const* imageFilePath);
	Image(IntVec2 size, Rgba8 color);
	Image(unsigned char* pixelData, int width, int height, int channels);
	~Image();

	std::string const& GetImageFilePath() const;
	IntVec2 GetDimensions() const;
	Rgba8 GetTexelColor(IntVec2 const& texelCoords) const;
	void SetTexelColor(IntVec2 const& texelCoords, Rgba8 const& newColor);

	const void* GetRawData() const;

	void SetName(std::string name);

private:
	std::string m_imageFilePath;
	IntVec2 m_dimensions = IntVec2(0, 0);
	int m_bytesPerTexel = 0;
	std::vector< Rgba8 > m_rgbaTexelsData; // or Rgba8* m_rgbaTexels = nullptr; if you prefer new[] and delete[]
};
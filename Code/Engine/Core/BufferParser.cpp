#include "Engine/Core/BufferParser.hpp"
#include "Engine/Core/ErrorWarningAssert.hpp"
#include "Engine/Math/Vec2.hpp"
#include "Engine/Math/Vec3.hpp"
#include "Engine/Math/Vec4.hpp"
#include "Engine/Math/IntVec2.hpp"
#include "Engine/Math/IntVec3.h"
#include "Engine/Math/IntVec4.h"
#include "Engine/Core/Rgba8.hpp"
#include "Engine/Math/AABB2.hpp"
#include "Engine/Math/OBB2.hpp"
#include "Engine/Math/Plane3.h"
#include "Engine/Core/Vertex_PCU.hpp"
#include "Engine/Core/Vertex_PCUTBN.hpp"

#include <cstring>
#include <algorithm>

BufferParser::BufferParser(const void* data, size_t size)
	: m_data(reinterpret_cast<const byte_t*>(data))
	, m_size(size)
	, m_readPos(0)
{
}

BufferParser::BufferParser(const std::vector<byte_t>& buffer)
	: m_data(buffer.data())
	, m_size(buffer.size())
	, m_readPos(0)
{
}

void BufferParser::SetEndianMode(EndianMode mode)
{
	m_endianMode = mode;
}

EndianMode BufferParser::GetEndianMode() const
{
	return m_endianMode;
}

bool BufferParser::IsNativeBigEndian()
{
	uint16_t test = 0x0102;
	return reinterpret_cast<byte_t*>(&test)[0] == 0x01;
}

bool BufferParser::ShouldSwap() const
{
	if (m_endianMode == EndianMode::NATIVE)
		return false;

	bool nativeBig = IsNativeBigEndian();
	bool targetBig = (m_endianMode == EndianMode::BIG);
	return nativeBig != targetBig;
}

void BufferParser::ReverseBytes(void* data, size_t size) const
{
	byte_t* bytes = reinterpret_cast<byte_t*>(data);
	for (size_t i = 0; i < size / 2; ++i)
	{
		std::swap(bytes[i], bytes[size - 1 - i]);
	}
}

void BufferParser::GuaranteeBytes(size_t numBytes) const
{
	if (m_readPos + numBytes > m_size)
	{
		ERROR_AND_DIE("BufferParser: attempted to read beyond end of buffer");
	}
}

void BufferParser::ParseRaw(void* out, size_t size)
{
	GuaranteeBytes(size);
	memcpy(out, m_data + m_readPos, size);
	m_readPos += size;
}


byte_t BufferParser::ParseByte()
{
	GuaranteeBytes(1);
	return m_data[m_readPos++];
}

char BufferParser::ParseChar()
{
	GuaranteeBytes(1);
	return static_cast<char>(m_data[m_readPos++]);
}

unsigned short BufferParser::ParseUshort()
{
	unsigned short value;
	ParseRaw(&value, sizeof(value));
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	return value;
}

short BufferParser::ParseShort()
{
	short value;
	ParseRaw(&value, sizeof(value));
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	return value;
}

uint32_t BufferParser::ParseUint32()
{
	uint32_t value;
	ParseRaw(&value, sizeof(value));
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	return value;
}

int32_t BufferParser::ParseInt32()
{
	int32_t value;
	ParseRaw(&value, sizeof(value));
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	return value;
}

uint64_t BufferParser::ParseUint64()
{
	uint64_t value;
	ParseRaw(&value, sizeof(value));
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	return value;
}

int64_t BufferParser::ParseInt64()
{
	int64_t value;
	ParseRaw(&value, sizeof(value));
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	return value;
}

float BufferParser::ParseFloat()
{
	float value;
	ParseRaw(&value, sizeof(value));
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	return value;
}

double BufferParser::ParseDouble()
{
	double value;
	ParseRaw(&value, sizeof(value));
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	return value;
}


std::string BufferParser::ParseStringZeroTerminated()
{
	std::string result;
	while (m_readPos < m_size)
	{
		byte_t c = m_data[m_readPos++];
		if (c == 0)
			break;
		result += static_cast<char>(c);
	}
	return result;
}

std::string BufferParser::ParseStringLengthPreceded()
{
	uint32_t length = ParseUint32();
	GuaranteeBytes(length);
	std::string result(reinterpret_cast<const char*>(m_data + m_readPos), length);
	m_readPos += length;
	return result;
}


Vec2 BufferParser::ParseVec2()
{
	float x = ParseFloat();
	float y = ParseFloat();
	return Vec2(x, y);
}

Vec3 BufferParser::ParseVec3()
{
	float x = ParseFloat();
	float y = ParseFloat();
	float z = ParseFloat();
	return Vec3(x, y, z);
}

Vec4 BufferParser::ParseVec4()
{
	float x = ParseFloat();
	float y = ParseFloat();
	float z = ParseFloat();
	float w = ParseFloat();
	return Vec4(x, y, z, w);
}

IntVec2 BufferParser::ParseIntVec2()
{
	int x = ParseInt32();
	int y = ParseInt32();
	return IntVec2(x, y);
}

IntVec3 BufferParser::ParseIntVec3()
{
	int x = ParseInt32();
	int y = ParseInt32();
	int z = ParseInt32();
	return IntVec3(x, y, z);
}

IntVec4 BufferParser::ParseIntVec4()
{
	int x = ParseInt32();
	int y = ParseInt32();
	int z = ParseInt32();
	int w = ParseInt32();
	return IntVec4(x, y, z, w);
}

Rgba8 BufferParser::ParseRgba8()
{
	byte_t r = ParseByte();
	byte_t g = ParseByte();
	byte_t b = ParseByte();
	byte_t a = ParseByte();
	return Rgba8(r, g, b, a);
}

AABB2 BufferParser::ParseAABB2()
{
	Vec2 mins = ParseVec2();
	Vec2 maxs = ParseVec2();
	return AABB2(mins, maxs);
}

OBB2 BufferParser::ParseOBB2()
{
	Vec2 center = ParseVec2();
	Vec2 iBasisNormal = ParseVec2();
	Vec2 halfDimensions = ParseVec2();
	return OBB2(center, iBasisNormal, halfDimensions);
}

Plane3 BufferParser::ParsePlane3()
{
	Vec3 normal = ParseVec3();
	float dist = ParseFloat();
	return Plane3(normal, dist);
}

Vertex_PCU BufferParser::ParseVertexPCU()
{
	Vec3 pos = ParseVec3();
	Rgba8 color = ParseRgba8();
	Vec2 uv = ParseVec2();
	return Vertex_PCU(pos, color, uv);
}

Vertex_PCUTBN BufferParser::ParseVertexPCUTBN()
{
	Vec3 pos = ParseVec3();
	Rgba8 color = ParseRgba8();
	Vec2 uv = ParseVec2();
	Vec3 tangent = ParseVec3();
	Vec3 bitangent = ParseVec3();
	Vec3 normal = ParseVec3();
	return Vertex_PCUTBN(pos, color, uv, tangent, bitangent, normal);
}


void BufferParser::SetReadPosition(size_t offset)
{
	if (offset > m_size)
	{
		ERROR_AND_DIE("BufferParser: attempted to seek beyond end of buffer");
	}
	m_readPos = offset;
}

size_t BufferParser::GetReadPosition() const
{
	return m_readPos;
}

size_t BufferParser::GetRemainingSize() const
{
	return m_size - m_readPos;
}

size_t BufferParser::GetTotalSize() const
{
	return m_size;
}

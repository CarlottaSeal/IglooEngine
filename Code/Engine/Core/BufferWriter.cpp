#include "Engine/Core/BufferWriter.hpp"
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

BufferWriter::BufferWriter(std::vector<byte_t>& buffer)
	: m_buffer(buffer)
{
}

void BufferWriter::SetEndianMode(EndianMode mode)
{
	m_endianMode = mode;
}

EndianMode BufferWriter::GetEndianMode() const
{
	return m_endianMode;
}

bool BufferWriter::IsNativeBigEndian()
{
	uint16_t test = 0x0102;
	return reinterpret_cast<byte_t*>(&test)[0] == 0x01;
}

bool BufferWriter::ShouldSwap() const
{
	if (m_endianMode == EndianMode::NATIVE)
		return false;

	bool nativeBig = IsNativeBigEndian();
	bool targetBig = (m_endianMode == EndianMode::BIG);
	return nativeBig != targetBig;
}

void BufferWriter::ReverseBytes(void* data, size_t size) const
{
	byte_t* bytes = reinterpret_cast<byte_t*>(data);
	for (size_t i = 0; i < size / 2; ++i)
	{
		std::swap(bytes[i], bytes[size - 1 - i]);
	}
}

void BufferWriter::AppendRaw(const void* data, size_t size)
{
	const byte_t* bytes = reinterpret_cast<const byte_t*>(data);
	m_buffer.insert(m_buffer.end(), bytes, bytes + size);
}

void BufferWriter::AppendByte(byte_t value)
{
	m_buffer.push_back(value);
}

void BufferWriter::AppendChar(char value)
{
	m_buffer.push_back(static_cast<byte_t>(value));
}

void BufferWriter::AppendUshort(unsigned short value)
{
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	AppendRaw(&value, sizeof(value));
}

void BufferWriter::AppendShort(short value)
{
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	AppendRaw(&value, sizeof(value));
}

void BufferWriter::AppendUint32(uint32_t value)
{
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	AppendRaw(&value, sizeof(value));
}

void BufferWriter::AppendInt32(int32_t value)
{
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	AppendRaw(&value, sizeof(value));
}

void BufferWriter::AppendUint64(uint64_t value)
{
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	AppendRaw(&value, sizeof(value));
}

void BufferWriter::AppendInt64(int64_t value)
{
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	AppendRaw(&value, sizeof(value));
}

void BufferWriter::AppendFloat(float value)
{
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	AppendRaw(&value, sizeof(value));
}

void BufferWriter::AppendDouble(double value)
{
	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));
	AppendRaw(&value, sizeof(value));
}


void BufferWriter::AppendStringZeroTerminated(const std::string& str)
{
	AppendRaw(str.c_str(), str.size());
	AppendByte(0);
}

void BufferWriter::AppendStringLengthPreceded(const std::string& str)
{
	AppendUint32(static_cast<uint32_t>(str.size()));
	AppendRaw(str.data(), str.size());
}


void BufferWriter::AppendVec2(const Vec2& v)
{
	AppendFloat(v.x);
	AppendFloat(v.y);
}

void BufferWriter::AppendVec3(const Vec3& v)
{
	AppendFloat(v.x);
	AppendFloat(v.y);
	AppendFloat(v.z);
}

void BufferWriter::AppendVec4(const Vec4& v)
{
	AppendFloat(v.x);
	AppendFloat(v.y);
	AppendFloat(v.z);
	AppendFloat(v.w);
}

void BufferWriter::AppendIntVec2(const IntVec2& v)
{
	AppendInt32(v.x);
	AppendInt32(v.y);
}

void BufferWriter::AppendIntVec3(const IntVec3& v)
{
	AppendInt32(v.x);
	AppendInt32(v.y);
	AppendInt32(v.z);
}

void BufferWriter::AppendIntVec4(const IntVec4& v)
{
	AppendInt32(v.x);
	AppendInt32(v.y);
	AppendInt32(v.z);
	AppendInt32(v.w);
}

void BufferWriter::AppendRgba8(const Rgba8& color)
{
	AppendByte(color.r);
	AppendByte(color.g);
	AppendByte(color.b);
	AppendByte(color.a);
}

void BufferWriter::AppendAABB2(const AABB2& box)
{
	AppendVec2(box.m_mins);
	AppendVec2(box.m_maxs);
}

void BufferWriter::AppendOBB2(const OBB2& obb)
{
	AppendVec2(obb.m_center);
	AppendVec2(obb.m_iBasisNormal);
	AppendVec2(obb.m_halfDimensions);
}

void BufferWriter::AppendPlane3(const Plane3& plane)
{
	AppendVec3(plane.m_normal);
	AppendFloat(plane.m_distToPlaneAloneNormalFromOrigin);
}

void BufferWriter::AppendVertexPCU(const Vertex_PCU& vert)
{
	AppendVec3(vert.m_position);
	AppendRgba8(vert.m_color);
	AppendVec2(vert.m_uvTextColors);
}

void BufferWriter::AppendVertexPCUTBN(const Vertex_PCUTBN& vert)
{
	AppendVec3(vert.m_position);
	AppendRgba8(vert.m_color);
	AppendVec2(vert.m_uvTexCoords);
	AppendVec3(vert.m_tangent);
	AppendVec3(vert.m_bitangent);
	AppendVec3(vert.m_normal);
}


void BufferWriter::OverwriteUint32(size_t offset, uint32_t value)
{
	if (offset + sizeof(uint32_t) > m_buffer.size())
		return;

	if (ShouldSwap())
		ReverseBytes(&value, sizeof(value));

	memcpy(&m_buffer[offset], &value, sizeof(uint32_t));
}


size_t BufferWriter::GetTotalSize() const
{
	return m_buffer.size();
}

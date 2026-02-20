#pragma once
#include "Engine/Core/BufferWriter.hpp"

struct Vec2;
struct Vec3;
struct Vec4;
struct IntVec2;
struct IntVec3;
struct IntVec4;
struct Rgba8;
struct AABB2;
struct OBB2;
struct Plane3;
struct Vertex_PCU;
struct Vertex_PCUTBN;

class BufferParser
{
public:
	BufferParser(const void* data, size_t size);
	BufferParser(const std::vector<byte_t>& buffer);

	void SetEndianMode(EndianMode mode);
	EndianMode GetEndianMode() const;

	byte_t ParseByte();
	char ParseChar();
	unsigned short ParseUshort();
	short ParseShort();
	uint32_t ParseUint32();
	int32_t ParseInt32();
	uint64_t ParseUint64();
	int64_t ParseInt64();
	float ParseFloat();
	double ParseDouble();

	std::string ParseStringZeroTerminated();
	std::string ParseStringLengthPreceded();

	Vec2 ParseVec2();
	Vec3 ParseVec3();
	Vec4 ParseVec4();
	IntVec2 ParseIntVec2();
	IntVec3 ParseIntVec3();
	IntVec4 ParseIntVec4();
	Rgba8 ParseRgba8();
	AABB2 ParseAABB2();
	OBB2 ParseOBB2();
	Plane3 ParsePlane3();
	Vertex_PCU ParseVertexPCU();
	Vertex_PCUTBN ParseVertexPCUTBN();

	void SetReadPosition(size_t offset);
	size_t GetReadPosition() const;
	size_t GetRemainingSize() const;
	size_t GetTotalSize() const;

private:
	void ParseRaw(void* out, size_t size);
	void ReverseBytes(void* data, size_t size) const;
	bool ShouldSwap() const;
	static bool IsNativeBigEndian();
	void GuaranteeBytes(size_t numBytes) const;

	const byte_t* m_data = nullptr;
	size_t m_size = 0;
	size_t m_readPos = 0;
	EndianMode m_endianMode = EndianMode::NATIVE;
};

#pragma once
#include <vector>
#include <string>
#include <cstdint>

typedef unsigned char byte_t;

enum class EndianMode
{
	NATIVE = 0,
	LITTLE,
	BIG
};

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

class BufferWriter
{
public:
	BufferWriter(std::vector<byte_t>& buffer);

	void SetEndianMode(EndianMode mode);
	EndianMode GetEndianMode() const;

	void AppendByte(byte_t value);
	void AppendChar(char value);
	void AppendUshort(unsigned short value);
	void AppendShort(short value);
	void AppendUint32(uint32_t value);
	void AppendInt32(int32_t value);
	void AppendUint64(uint64_t value);
	void AppendInt64(int64_t value);
	void AppendFloat(float value);
	void AppendDouble(double value);

	void AppendStringZeroTerminated(const std::string& str);
	void AppendStringLengthPreceded(const std::string& str);

	void AppendVec2(const Vec2& v);
	void AppendVec3(const Vec3& v);
	void AppendVec4(const Vec4& v);
	void AppendIntVec2(const IntVec2& v);
	void AppendIntVec3(const IntVec3& v);
	void AppendIntVec4(const IntVec4& v);
	void AppendRgba8(const Rgba8& color);
	void AppendAABB2(const AABB2& box);
	void AppendOBB2(const OBB2& obb);
	void AppendPlane3(const Plane3& plane);
	void AppendVertexPCU(const Vertex_PCU& vert);
	void AppendVertexPCUTBN(const Vertex_PCUTBN& vert);

	void OverwriteUint32(size_t offset, uint32_t value);

	size_t GetTotalSize() const;

private:
	void AppendRaw(const void* data, size_t size);
	void ReverseBytes(void* data, size_t size) const;
	bool ShouldSwap() const;
	static bool IsNativeBigEndian();

	std::vector<byte_t>& m_buffer;
	EndianMode m_endianMode = EndianMode::NATIVE;
};

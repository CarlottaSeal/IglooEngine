#include "Mat44.hpp"
#include "Vec2.hpp"
#include "MathUtils.hpp"
#include <math.h>

Mat44::Mat44()
{
	m_values[Ix] = 1.0f; m_values[Iy] = 0.0f; m_values[Iz] = 0.0f; m_values[Iw] = 0.0f;
	m_values[Jx] = 0.0f; m_values[Jy] = 1.0f; m_values[Jz] = 0.0f; m_values[Jw] = 0.0f;
	m_values[Kx] = 0.0f; m_values[Ky] = 0.0f; m_values[Kz] = 1.0f; m_values[Kw] = 0.0f;
	m_values[Tx] = 0.0f; m_values[Ty] = 0.0f; m_values[Tz] = 0.0f; m_values[Tw] = 1.0f;
}

Mat44::Mat44(Vec2 const& iBasis2D, Vec2 const& jBasis2D, Vec2 const& translation2D)
{
    //2D translation matrix
	m_values[Ix] = iBasis2D.x;      m_values[Iy] = iBasis2D.y;      m_values[Iz] = 0.0f;	m_values[Iw] = 0.0f;
	m_values[Jx] = jBasis2D.x;      m_values[Jy] = jBasis2D.y;      m_values[Jz] = 0.0f;	m_values[Jw] = 0.0f;
	m_values[Kx] = 0.0f;            m_values[Ky] = 0.0f;            m_values[Kz] = 1.0f;	m_values[Kw] = 0.0f;
	m_values[Tx] = translation2D.x; m_values[Ty] = translation2D.y; m_values[Tz] = 0.0f;	m_values[Tw] = 1.0f;
}

Mat44::Mat44(Vec3 const& iBasis3D, Vec3 const& jBasis3D, Vec3 const& kBasis3D, Vec3 const& translation3D)
{
	m_values[Ix] = iBasis3D.x;      m_values[Iy] = iBasis3D.y;      m_values[Iz] = iBasis3D.z;      m_values[Iw] = 0.0f;
	m_values[Jx] = jBasis3D.x;      m_values[Jy] = jBasis3D.y;      m_values[Jz] = jBasis3D.z;      m_values[Jw] = 0.0f;
	m_values[Kx] = kBasis3D.x;      m_values[Ky] = kBasis3D.y;      m_values[Kz] = kBasis3D.z;      m_values[Kw] = 0.0f;
	m_values[Tx] = translation3D.x; m_values[Ty] = translation3D.y; m_values[Tz] = translation3D.z; m_values[Tw] = 1.0f;
}

Mat44::Mat44(Vec4 const& iBasis4D, Vec4 const& jBasis4D, Vec4 const& kBasis4D, Vec4 const& translation4D)
{
	m_values[Ix] = iBasis4D.x;		m_values[Iy] = iBasis4D.y;		m_values[Iz] = iBasis4D.z;		m_values[Iw] = iBasis4D.w;
	m_values[Jx] = jBasis4D.x;		m_values[Jy] = jBasis4D.y;		m_values[Jz] = jBasis4D.z;		m_values[Jw] = jBasis4D.w;
	m_values[Kx] = kBasis4D.x;		m_values[Ky] = kBasis4D.y;		m_values[Kz] = kBasis4D.z;		m_values[Kw] = kBasis4D.w;
	m_values[Tx] = translation4D.x; m_values[Ty] = translation4D.y; m_values[Tz] = translation4D.z; m_values[Tw] = translation4D.w;
}

Mat44::Mat44(float const* sixteenValuesBasisMajor)
{
	for (int i = 0; i < 16; ++i)
	{
		m_values[i] = sixteenValuesBasisMajor[i];
	} //pass in 16 values
}

Mat44 const Mat44::MakeTranslation2D(Vec2 const& translationXY)
{
	Mat44 resultMatrix;
    resultMatrix.m_values[Tx] = translationXY.x; 
    resultMatrix.m_values[Ty] = translationXY.y; 
	resultMatrix.m_values[Tz] = 0.f;
	resultMatrix.m_values[Tw] = 1.0f;
	return resultMatrix;
}

Mat44 const Mat44::MakeTranslation3D(Vec3 const& translationXYZ)
{
	Mat44 resultMatrix;
	resultMatrix.m_values[Tx] = translationXYZ.x;
	resultMatrix.m_values[Ty] = translationXYZ.y;
    resultMatrix.m_values[Tz] = translationXYZ.z;
	resultMatrix.m_values[Tw] = 1.0f;
	return resultMatrix;
}

Mat44 const Mat44::MakeUniformScale2D(float uniformXY)
{
	Mat44 resultMatrix;
	resultMatrix.m_values[Ix] = uniformXY; 
	resultMatrix.m_values[Jy] = uniformXY; 
	return resultMatrix;
}

Mat44 const Mat44::MakeUniformScale3D(float uniformXYZ)
{
	Mat44 resultMatrix;
	resultMatrix.m_values[Ix] = uniformXYZ;
	resultMatrix.m_values[Jy] = uniformXYZ;
    resultMatrix.m_values[Kz] = uniformXYZ;
	resultMatrix.m_values[Tw] = 1.0f;
	return resultMatrix;
}

Mat44 const Mat44::MakeNonUniformScale2D(Vec2 const& nonUniformScaleXY)
{
	Mat44 resultMatrix;
	resultMatrix.m_values[Ix] = nonUniformScaleXY.x;
	resultMatrix.m_values[Jy] = nonUniformScaleXY.y;
	return resultMatrix;
}

Mat44 const Mat44::MakeNonUniformScale3D(Vec3 const& nonUniformScaleXYZ)
{
	Mat44 resultMatrix;
	resultMatrix.m_values[Ix] = nonUniformScaleXYZ.x;
	resultMatrix.m_values[Jy] = nonUniformScaleXYZ.y;
	resultMatrix.m_values[Kz] = nonUniformScaleXYZ.z;
	return resultMatrix;
}

Mat44 const Mat44::MakeZRotationDegrees(float rotationDegreesAboutZ)
{
	Mat44 resultMatrix;
	float radians = ConvertDegreesToRadians(rotationDegreesAboutZ);
	float cosTheta = cosf(radians);
	float sinTheta = sinf(radians);

	resultMatrix.m_values[Ix] = cosTheta;  
	resultMatrix.m_values[Iy] = sinTheta; 
	resultMatrix.m_values[Jx] = -sinTheta;  
	resultMatrix.m_values[Jy] = cosTheta;  

	resultMatrix.m_values[Kz] = 1.0f;
	resultMatrix.m_values[Tw] = 1.0f;
	return resultMatrix;
}

Mat44 const Mat44::MakeYRotationDegrees(float rotationDegreesAboutY)
{
	Mat44 resultMatrix;
	float radians = ConvertDegreesToRadians(rotationDegreesAboutY);
	float cosTheta = cosf(radians);
	float sinTheta = sinf(radians);

	resultMatrix.m_values[Ix] = cosTheta;
	resultMatrix.m_values[Iz] = -sinTheta;
	resultMatrix.m_values[Kx] = sinTheta;
	resultMatrix.m_values[Kz] = cosTheta;

	resultMatrix.m_values[Jy] = 1.0f;
	resultMatrix.m_values[Tw] = 1.0f;
	return resultMatrix;
}

Mat44 const Mat44::MakeXRotationDegrees(float rotationDegreesAboutX)
{
	Mat44 resultMatrix;
	float radians = ConvertDegreesToRadians(rotationDegreesAboutX);
	float cosTheta = cosf(radians);
	float sinTheta = sinf(radians);

	resultMatrix.m_values[Jy] = cosTheta;
	resultMatrix.m_values[Jz] = sinTheta;
	resultMatrix.m_values[Ky] = -sinTheta;
	resultMatrix.m_values[Kz] = cosTheta;

	resultMatrix.m_values[Ix] = 1.0f;
	resultMatrix.m_values[Tw] = 1.0f;
	return resultMatrix;
}

Mat44 const Mat44::MakeOrthoProjection(float left, float right, float bottom, float top, float zNear, float zFar)
{
	Mat44 orthoMat;
	orthoMat.m_values[Ix] = 2.f / (right - left);
	orthoMat.m_values[Tx] = -(right + left) / (right - left); // 2.f;//* ;
	orthoMat.m_values[Jy] = 2.f / (top - bottom);
	orthoMat.m_values[Ty] = -(top + bottom) / (top - bottom); // 2.f;//* ;
	orthoMat.m_values[Kz] = 1.f / (zFar - zNear);
	orthoMat.m_values[Tz] = -zNear/ (zFar - zNear);

	return orthoMat;
}

Mat44 const Mat44::MakePerspectiveProjection(float fovYDegrees, float aspect, float zNear, float zFar)
{

	Mat44 perspectMat;
	////homogeneous
	//if (zNear == 0.f)
	//{
	//	zNear = 0.00001f;
	//}
	//perspectMat.m_values[Tw] = 0.f;
	//perspectMat.m_values[Kw] = 1.f;
	//perspectMat.m_values[Kz] = zNear + zFar;
	//perspectMat.m_values[Tz] = -zFar * zNear;
	//perspectMat.m_values[Ix] = zNear;
	//perspectMat.m_values[Jy] = zNear;

	//float tanHalfFovY = tanf(ConvertDegreesToRadians(fovYDegrees) / 2.0f);
	//float halfY = tanHalfFovY * zNear;
	//float halfX = halfY * aspect;

	//Mat44 orthoMat = MakeOrthoProjection(-halfX, halfX, -halfY, halfY, zNear, zFar);
	//orthoMat.Append(perspectMat);   
	//return orthoMat;             //first frustrum->box, next orthorize, but there's a NaN problem


	//or I will push the result:
	float tanHalfFovY = tanf(ConvertDegreesToRadians(fovYDegrees) / 2.0f);
	float tanHalfFovX = tanHalfFovY * aspect;

	perspectMat.m_values[Ix] = 1.0f / tanHalfFovX; 
	perspectMat.m_values[Jy] = 1.0f / tanHalfFovY; 
	perspectMat.m_values[Kz] = zFar / (zFar - zNear); 
	perspectMat.m_values[Kw] = 1.0f; 
	perspectMat.m_values[Tz] = -zNear * zFar / (zFar - zNear); 
	perspectMat.m_values[Tw] = 0.0f;

	return perspectMat;
}

Mat44 const Mat44::CreateLookAt(Vec3 const& eye, Vec3 const& target, Vec3 const& worldUp)
{
	// 1) Forward (I basis) - 从眼睛指向目标
	Vec3 i = (target - eye).GetNormalized();
	// 2) 选择一个不与 forward 共线的 up
	Vec3 up = worldUp;
	if (fabsf(DotProduct3D(i, up)) > 0.999f)
	{
		// 根据 forward 选择备用 up
		up = (fabsf(i.y) < 0.9f) ? Vec3(0.f, 1.f, 0.f) : Vec3(1.f, 0.f, 0.f);
	}
	// 3) Left (J basis) = up × forward
	Vec3 j = CrossProduct3D(up, i).GetNormalized();
	// 4) 重新计算 Up (K basis) = forward × left，确保完全正交
	Vec3 k = CrossProduct3D(i, j); // 已经是单位向量，不需要再 normalize
	// 5) 构建 camera-to-world 矩阵，然后取逆
	Mat44 camToWorld;
	camToWorld.SetIJKT3D(i, j, k, eye);
    
	return camToWorld.GetOrthonormalInverse();
}

Vec2 const Mat44::TransformVectorQuantity2D(Vec2 const& vectorQuantityXY) const
{
    //assumes z=0, w=0
	float x = m_values[Ix] * vectorQuantityXY.x + m_values[Jx] * vectorQuantityXY.y;
	float y = m_values[Iy] * vectorQuantityXY.x + m_values[Jy] * vectorQuantityXY.y;
	return Vec2(x, y);
}

Vec3 const Mat44::TransformVectorQuantity3D(Vec3 const& vectorQuantityXYZ) const
{
    //assumes w=0
	float x = m_values[Ix] * vectorQuantityXYZ.x + m_values[Jx] * vectorQuantityXYZ.y + m_values[Kx] * vectorQuantityXYZ.z;
	float y = m_values[Iy] * vectorQuantityXYZ.x + m_values[Jy] * vectorQuantityXYZ.y + m_values[Ky] * vectorQuantityXYZ.z;
	float z = m_values[Iz] * vectorQuantityXYZ.x + m_values[Jz] * vectorQuantityXYZ.y + m_values[Kz] * vectorQuantityXYZ.z;
	return Vec3(x, y, z);
}

Vec2 const Mat44::TransformPosition2D(Vec2 const& positionXY) const
{
    //assumes z=0, w=1
	float x = m_values[Ix] * positionXY.x + m_values[Jx] * positionXY.y + m_values[Tx];
	float y = m_values[Iy] * positionXY.x + m_values[Jy] * positionXY.y + m_values[Ty];
	return Vec2(x, y);
}

Vec3 const Mat44::TransformPosition3D(Vec3 const& position3D) const
{
    //assumes w=1
	float x = m_values[Ix] * position3D.x + m_values[Jx] * position3D.y + m_values[Kx] * position3D.z + m_values[Tx];
	float y = m_values[Iy] * position3D.x + m_values[Jy] * position3D.y + m_values[Ky] * position3D.z + m_values[Ty];
	float z = m_values[Iz] * position3D.x + m_values[Jz] * position3D.y + m_values[Kz] * position3D.z + m_values[Tz];
	return Vec3(x, y, z);
}

Vec4 const Mat44::TransformHomogeneous3D(Vec4 const& homogeneousPoint3D) const
{
    //w is provided
	float x = m_values[Ix] * homogeneousPoint3D.x + m_values[Jx] * homogeneousPoint3D.y + m_values[Kx] * homogeneousPoint3D.z + m_values[Tx] * homogeneousPoint3D.w;
	float y = m_values[Iy] * homogeneousPoint3D.x + m_values[Jy] * homogeneousPoint3D.y + m_values[Ky] * homogeneousPoint3D.z + m_values[Ty] * homogeneousPoint3D.w;
	float z = m_values[Iz] * homogeneousPoint3D.x + m_values[Jz] * homogeneousPoint3D.y + m_values[Kz] * homogeneousPoint3D.z + m_values[Tz] * homogeneousPoint3D.w;
	float w = m_values[Iw] * homogeneousPoint3D.x + m_values[Jw] * homogeneousPoint3D.y + m_values[Kw] * homogeneousPoint3D.z + m_values[Tw] * homogeneousPoint3D.w;
	return Vec4(x, y, z, w);
}

float* Mat44::GetAsFloatArray()
{
    //non-const(mutable) version
	return m_values;
}

float const* Mat44::GetAsFloatArray() const
{
    //const version, used only when Mat44 is const
	return m_values;
}

Vec2 const Mat44::GetIBasis2D() const
{
	return Vec2(m_values[Ix], m_values[Iy]);
}

Vec2 const Mat44::GetJBasis2D() const
{
	return Vec2(m_values[Jx], m_values[Jy]);
}

Vec2 const Mat44::GetTranslation2D() const
{
	return Vec2(m_values[Tx], m_values[Ty]);
}

Vec3 const Mat44::GetIBasis3D() const
{
	return Vec3(m_values[Ix], m_values[Iy], m_values[Iz]);
}

Vec3 const Mat44::GetJBasis3D() const
{
	return Vec3(m_values[Jx], m_values[Jy], m_values[Jz]);
}

Vec3 const Mat44::GetKBasis3D() const
{
	return Vec3(m_values[Kx], m_values[Ky], m_values[Kz]);
}

Vec3 const Mat44::GetTranslation3D() const
{
	return Vec3(m_values[Tx], m_values[Ty], m_values[Tz]);
}

Vec4 const Mat44::GetIBasis4D() const
{
	return Vec4(m_values[Ix], m_values[Iy], m_values[Iz], m_values[Iw]);
}

Vec4 const Mat44::GetJBasis4D() const
{
	return Vec4(m_values[Jx], m_values[Jy], m_values[Jz], m_values[Jw]);
}

Vec4 const Mat44::GetKBasis4D() const
{
	return Vec4(m_values[Kx], m_values[Ky], m_values[Kz], m_values[Kw]);
}

Vec4 const Mat44::GetTranslation4D() const
{
	return Vec4(m_values[Tx], m_values[Ty], m_values[Tz], m_values[Tw]);
}

Mat44 const Mat44::GetOrthonormalInverse() const
{
	Mat44 inverse;
	Vec4 translation = GetTranslation4D();

	inverse.m_values[Ix] = m_values[Ix];
	inverse.m_values[Iy] = m_values[Jx];
	inverse.m_values[Iz] = m_values[Kx];

	inverse.m_values[Jx] = m_values[Iy];
	inverse.m_values[Jy] = m_values[Jy];
	inverse.m_values[Jz] = m_values[Ky];

	inverse.m_values[Kx] = m_values[Iz];
	inverse.m_values[Ky] = m_values[Jz];
	inverse.m_values[Kz] = m_values[Kz];

	//inverse_translation=−(rotation_inverse⋅translation)
	inverse.m_values[Tx] = -(translation.x * inverse.m_values[Ix] + translation.y * inverse.m_values[Jx] + translation.z * inverse.m_values[Kx]);
	inverse.m_values[Ty] = -(translation.x * inverse.m_values[Iy] + translation.y * inverse.m_values[Jy] + translation.z * inverse.m_values[Ky]);
	inverse.m_values[Tz] = -(translation.x * inverse.m_values[Iz] + translation.y * inverse.m_values[Jz] + translation.z * inverse.m_values[Kz]);

	return inverse;
}

Mat44 const Mat44::GetInverse() const
{
	Mat44 inv;
	float det;

	float s0 = m_values[0] * m_values[5] - m_values[4] * m_values[1];
	float s1 = m_values[0] * m_values[6] - m_values[4] * m_values[2];
	float s2 = m_values[0] * m_values[7] - m_values[4] * m_values[3];
	float s3 = m_values[1] * m_values[6] - m_values[5] * m_values[2];
	float s4 = m_values[1] * m_values[7] - m_values[5] * m_values[3];
	float s5 = m_values[2] * m_values[7] - m_values[6] * m_values[3];

	float c5 = m_values[10] * m_values[15] - m_values[14] * m_values[11];
	float c4 = m_values[9] * m_values[15] - m_values[13] * m_values[11];
	float c3 = m_values[9] * m_values[14] - m_values[13] * m_values[10];
	float c2 = m_values[8] * m_values[15] - m_values[12] * m_values[11];
	float c1 = m_values[8] * m_values[14] - m_values[12] * m_values[10];
	float c0 = m_values[8] * m_values[13] - m_values[12] * m_values[9];

	det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;

	if (abs(det) < 0.0001f)
	{
		return Mat44();
	}

	float invdet = 1.0f / det;

	inv.m_values[0] = (m_values[5] * c5 - m_values[6] * c4 + m_values[7] * c3) * invdet;
	inv.m_values[1] = (-m_values[1] * c5 + m_values[2] * c4 - m_values[3] * c3) * invdet;
	inv.m_values[2] = (m_values[13] * s5 - m_values[14] * s4 + m_values[15] * s3) * invdet;
	inv.m_values[3] = (-m_values[9] * s5 + m_values[10] * s4 - m_values[11] * s3) * invdet;

	inv.m_values[4] = (-m_values[4] * c5 + m_values[6] * c2 - m_values[7] * c1) * invdet;
	inv.m_values[5] = (m_values[0] * c5 - m_values[2] * c2 + m_values[3] * c1) * invdet;
	inv.m_values[6] = (-m_values[12] * s5 + m_values[14] * s2 - m_values[15] * s1) * invdet;
	inv.m_values[7] = (m_values[8] * s5 - m_values[10] * s2 + m_values[11] * s1) * invdet;

	inv.m_values[8] = (m_values[4] * c4 - m_values[5] * c2 + m_values[7] * c0) * invdet;
	inv.m_values[9] = (-m_values[0] * c4 + m_values[1] * c2 - m_values[3] * c0) * invdet;
	inv.m_values[10] = (m_values[12] * s4 - m_values[13] * s2 + m_values[15] * s0) * invdet;
	inv.m_values[11] = (-m_values[8] * s4 + m_values[9] * s2 - m_values[11] * s0) * invdet;

	inv.m_values[12] = (-m_values[4] * c3 + m_values[5] * c1 - m_values[6] * c0) * invdet;
	inv.m_values[13] = (m_values[0] * c3 - m_values[1] * c1 + m_values[2] * c0) * invdet;
	inv.m_values[14] = (-m_values[12] * s3 + m_values[13] * s1 - m_values[14] * s0) * invdet;
	inv.m_values[15] = (m_values[8] * s3 - m_values[9] * s1 + m_values[10] * s0) * invdet;

	return inv;
}

void Mat44::SetTranslation2D(Vec2 const& translationXY)
{
    //Set translationZ = 0, translationW = 1
	m_values[Tx] = translationXY.x;
	m_values[Ty] = translationXY.y;
	m_values[Tz] = 0.0f;
	m_values[Tw] = 1.0f;
}

void Mat44::SetTranslation3D(Vec3 const& translationXYZ)
{
    //Set translationW = 1
	m_values[Tx] = translationXYZ.x;
	m_values[Ty] = translationXYZ.y;
	m_values[Tz] = translationXYZ.z;
	m_values[Tw] = 1.0f;
}

void Mat44::SetIJ2D(Vec2 const& iBasis2D, Vec2 const& jBasis2D)
{
    //Set z = 0, w=0 for i&j; does not modify k or t
	m_values[Ix] = iBasis2D.x;
	m_values[Iy] = iBasis2D.y;
	m_values[Iz] = 0.0f;
	m_values[Iw] = 0.0f;

	m_values[Jx] = jBasis2D.x;
	m_values[Jy] = jBasis2D.y;
	m_values[Jz] = 0.0f;
	m_values[Jw] = 0.0f;
}

void Mat44::SetIJT2D(Vec2 const& iBasis2D, Vec2 const& jBasis2D, Vec2 const& translationXY)
{
    //Set z = 0, w=0 for i&j, w = 1 for t; does not modify k
	m_values[Ix] = iBasis2D.x;
	m_values[Iy] = iBasis2D.y;
	m_values[Iz] = 0.0f;
	m_values[Iw] = 0.0f;

	m_values[Jx] = jBasis2D.x;
	m_values[Jy] = jBasis2D.y;
	m_values[Jz] = 0.0f;
	m_values[Jw] = 0.0f;

	m_values[Tx] = translationXY.x;
	m_values[Ty] = translationXY.y;
	m_values[Tz] = 0.0f;
	m_values[Tw] = 1.0f;
}

void Mat44::SetIJK3D(Vec3 const& iBasis3D, Vec3 const& jBasis3D, Vec3 const& kBasis3D)
{
    //Set w=0 for i&j&k, does not modify t
	m_values[Ix] = iBasis3D.x;
	m_values[Iy] = iBasis3D.y;
	m_values[Iz] = iBasis3D.z;
	m_values[Iw] = 0.0f;

	m_values[Jx] = jBasis3D.x;
	m_values[Jy] = jBasis3D.y;
	m_values[Jz] = jBasis3D.z;
	m_values[Jw] = 0.0f;

	m_values[Kx] = kBasis3D.x;
	m_values[Ky] = kBasis3D.y;
	m_values[Kz] = kBasis3D.z;
	m_values[Kw] = 0.0f;
}

void Mat44::SetIJKT3D(Vec3 const& iBasis3D, Vec3 const& jBasis3D, Vec3 const& kBasis3D, Vec3 const& translationXYZ)
{
    //Set w=0 for i&j&k, w=1 for t
	m_values[Ix] = iBasis3D.x;
	m_values[Iy] = iBasis3D.y;
	m_values[Iz] = iBasis3D.z;
	m_values[Iw] = 0.0f;

	m_values[Jx] = jBasis3D.x;
	m_values[Jy] = jBasis3D.y;
	m_values[Jz] = jBasis3D.z;
	m_values[Jw] = 0.0f;

	m_values[Kx] = kBasis3D.x;
	m_values[Ky] = kBasis3D.y;
	m_values[Kz] = kBasis3D.z;
	m_values[Kw] = 0.0f;

	m_values[Tx] = translationXYZ.x;
	m_values[Ty] = translationXYZ.y;
	m_values[Tz] = translationXYZ.z;
	m_values[Tw] = 1.0f;
}

void Mat44::SetIJKT4D(Vec4 const& iBasis4D, Vec4 const& jBasis4D, Vec4 const& kBasis4D, Vec4 const& translation4D)
{
    //All 16 values provided
	m_values[Ix] = iBasis4D.x;
	m_values[Iy] = iBasis4D.y;
	m_values[Iz] = iBasis4D.z;
	m_values[Iw] = iBasis4D.w;

	m_values[Jx] = jBasis4D.x;
	m_values[Jy] = jBasis4D.y;
	m_values[Jz] = jBasis4D.z;
	m_values[Jw] = jBasis4D.w;

	m_values[Kx] = kBasis4D.x;
	m_values[Ky] = kBasis4D.y;
	m_values[Kz] = kBasis4D.z;
	m_values[Kw] = kBasis4D.w;

	m_values[Tx] = translation4D.x;
	m_values[Ty] = translation4D.y;
	m_values[Tz] = translation4D.z;
	m_values[Tw] = translation4D.w;
}

void Mat44::Transpose()
{
	float oldIy = m_values[Iy], oldIz = m_values[Iz], oldIw = m_values[Iw];
	float oldJx = m_values[Jx], oldJz = m_values[Jz], oldJw = m_values[Jw];
	float oldKx = m_values[Kx], oldKy = m_values[Ky], oldKw = m_values[Kw];
	float oldTx = m_values[Tx], oldTy = m_values[Ty], oldTz = m_values[Tz];

	m_values[Iy] = oldJx;
	m_values[Iz] = oldKx;
	m_values[Iw] = oldTx;
	m_values[Jx] = oldIy;
	m_values[Jz] = oldKy;
	m_values[Jw] = oldTy;
	m_values[Kx] = oldIz;
	m_values[Ky] = oldJz;
	m_values[Kw] = oldTz;
	m_values[Tx] = oldIw;
	m_values[Ty] = oldJw;
	m_values[Tz] = oldKw;
}

void Mat44::Orthonormalize_IFwd_JLeft_KUp()
{
	Vec3 iBasis(m_values[Ix], m_values[Iy], m_values[Iz]);
	iBasis = iBasis.GetNormalized();
	m_values[Iw] = 0.f;

	Vec3 kBasis(m_values[Kx], m_values[Ky], m_values[Kz]);
	kBasis = kBasis - (DotProduct3D(kBasis, iBasis) * iBasis);
	kBasis = kBasis.GetNormalized();
	m_values[Kw] = 0.f;

	Vec3 jBasis(m_values[Jx], m_values[Jy], m_values[Jz]);
	jBasis = jBasis - (DotProduct3D(jBasis, iBasis) * iBasis) - (DotProduct3D(jBasis, kBasis) * kBasis);
	jBasis = jBasis.GetNormalized();

	m_values[Ix] = iBasis.x; m_values[Iy] = iBasis.y; m_values[Iz] = iBasis.z;
	m_values[Jx] = jBasis.x; m_values[Jy] = jBasis.y; m_values[Jz] = jBasis.z;
	m_values[Kx] = kBasis.x; m_values[Ky] = kBasis.y; m_values[Kz] = kBasis.z;
}

void Mat44::Append(Mat44 const& appendThis) //右乘 //A append B append C: [A][B][C]
{
    //multiply on right in column notation/on left in row notation
	float oldIx = m_values[Ix], oldIy = m_values[Iy], oldIz = m_values[Iz], oldIw = m_values[Iw];
	float oldJx = m_values[Jx], oldJy = m_values[Jy], oldJz = m_values[Jz], oldJw = m_values[Jw];
	float oldKx = m_values[Kx], oldKy = m_values[Ky], oldKz = m_values[Kz], oldKw = m_values[Kw];
	float oldTx = m_values[Tx], oldTy = m_values[Ty], oldTz = m_values[Tz], oldTw = m_values[Tw];

	// use appendThis' row x this' column
	m_values[Ix] = appendThis.m_values[Ix] * oldIx + appendThis.m_values[Iy] * oldJx + appendThis.m_values[Iz] * oldKx + appendThis.m_values[Iw] * oldTx;
	m_values[Iy] = appendThis.m_values[Ix] * oldIy + appendThis.m_values[Iy] * oldJy + appendThis.m_values[Iz] * oldKy + appendThis.m_values[Iw] * oldTy;
	m_values[Iz] = appendThis.m_values[Ix] * oldIz + appendThis.m_values[Iy] * oldJz + appendThis.m_values[Iz] * oldKz + appendThis.m_values[Iw] * oldTz;
	m_values[Iw] = appendThis.m_values[Ix] * oldIw + appendThis.m_values[Iy] * oldJw + appendThis.m_values[Iz] * oldKw + appendThis.m_values[Iw] * oldTw;

	m_values[Jx] = appendThis.m_values[Jx] * oldIx + appendThis.m_values[Jy] * oldJx + appendThis.m_values[Jz] * oldKx + appendThis.m_values[Jw] * oldTx;
	m_values[Jy] = appendThis.m_values[Jx] * oldIy + appendThis.m_values[Jy] * oldJy + appendThis.m_values[Jz] * oldKy + appendThis.m_values[Jw] * oldTy;
	m_values[Jz] = appendThis.m_values[Jx] * oldIz + appendThis.m_values[Jy] * oldJz + appendThis.m_values[Jz] * oldKz + appendThis.m_values[Jw] * oldTz;
	m_values[Jw] = appendThis.m_values[Jx] * oldIw + appendThis.m_values[Jy] * oldJw + appendThis.m_values[Jz] * oldKw + appendThis.m_values[Jw] * oldTw;

	m_values[Kx] = appendThis.m_values[Kx] * oldIx + appendThis.m_values[Ky] * oldJx + appendThis.m_values[Kz] * oldKx + appendThis.m_values[Kw] * oldTx;
	m_values[Ky] = appendThis.m_values[Kx] * oldIy + appendThis.m_values[Ky] * oldJy + appendThis.m_values[Kz] * oldKy + appendThis.m_values[Kw] * oldTy;
	m_values[Kz] = appendThis.m_values[Kx] * oldIz + appendThis.m_values[Ky] * oldJz + appendThis.m_values[Kz] * oldKz + appendThis.m_values[Kw] * oldTz;
	m_values[Kw] = appendThis.m_values[Kx] * oldIw + appendThis.m_values[Ky] * oldJw + appendThis.m_values[Kz] * oldKw + appendThis.m_values[Kw] * oldTw;

	m_values[Tx] = appendThis.m_values[Tx] * oldIx + appendThis.m_values[Ty] * oldJx + appendThis.m_values[Tz] * oldKx + appendThis.m_values[Tw] * oldTx;
	m_values[Ty] = appendThis.m_values[Tx] * oldIy + appendThis.m_values[Ty] * oldJy + appendThis.m_values[Tz] * oldKy + appendThis.m_values[Tw] * oldTy;
	m_values[Tz] = appendThis.m_values[Tx] * oldIz + appendThis.m_values[Ty] * oldJz + appendThis.m_values[Tz] * oldKz + appendThis.m_values[Tw] * oldTz;
	m_values[Tw] = appendThis.m_values[Tx] * oldIw + appendThis.m_values[Ty] * oldJw + appendThis.m_values[Tz] * oldKw + appendThis.m_values[Tw] * oldTw;
}

void Mat44::AppendZRotation(float degreesRotationAboutZ)
{
    //same as appending (*= in column notation) a z_rotation matrix
	/*Mat44 rotation = MakeZRotationDegrees(degreesRotationAboutZ);
	Append(rotation);*/
	float radians = ConvertDegreesToRadians(degreesRotationAboutZ);
	float cosTheta = cosf(radians);
	float sinTheta = sinf(radians);

	float oldIx = m_values[Ix], oldIy = m_values[Iy], oldIz = m_values[Iz], oldIw = m_values[Iw];
	float oldJx = m_values[Jx], oldJy = m_values[Jy], oldJz = m_values[Jz], oldJw = m_values[Jw];

	m_values[Ix] = oldIx * cosTheta + oldJx * sinTheta;
	m_values[Iy] = oldIy * cosTheta + oldJy * sinTheta;
	m_values[Iz] = oldIz * cosTheta + oldJz * sinTheta;
	m_values[Iw] = oldIw * cosTheta + oldJw * sinTheta;

	m_values[Jx] = oldJx * cosTheta - oldIx * sinTheta;
	m_values[Jy] = oldJy * cosTheta - oldIy * sinTheta;
	m_values[Jz] = oldJz * cosTheta - oldIz * sinTheta;
	m_values[Jw] = oldJw * cosTheta - oldIw * sinTheta;
}

void Mat44::AppendYRotation(float degreesRotationAboutY)
{
    //same as appending (*= in column notation) a y_rotation matrix
	float radians = ConvertDegreesToRadians(degreesRotationAboutY);
	float cosTheta = cosf(radians);
	float sinTheta = sinf(radians);

	float oldIx = m_values[Ix], oldIy = m_values[Iy], oldIz = m_values[Iz], oldIw = m_values[Iw];
	float oldKx = m_values[Kx], oldKy = m_values[Ky], oldKz = m_values[Kz], oldKw = m_values[Kw];

	m_values[Ix] = oldIx * cosTheta - oldKx * sinTheta;
	m_values[Iy] = oldIy * cosTheta - oldKy * sinTheta;
	m_values[Iz] = oldIz * cosTheta - oldKz * sinTheta;
	m_values[Iw] = oldIw * cosTheta - oldKw * sinTheta;

	m_values[Kx] = oldIx * sinTheta + oldKx * cosTheta;
	m_values[Ky] = oldIy * sinTheta + oldKy * cosTheta;
	m_values[Kz] = oldIz * sinTheta + oldKz * cosTheta;
	m_values[Kw] = oldIw * sinTheta + oldKw * cosTheta;
}

void Mat44::AppendXRotation(float degreesRotationAboutX)
{
    //same as appending (*= in column notation) a x_rotation matrix
	float radians = ConvertDegreesToRadians(degreesRotationAboutX);
	float cosTheta = cosf(radians);
	float sinTheta = sinf(radians);

	float oldJx = m_values[Jx], oldJy = m_values[Jy], oldJz = m_values[Jz], oldJw = m_values[Jw];
	float oldKx = m_values[Kx], oldKy = m_values[Ky], oldKz = m_values[Kz], oldKw = m_values[Kw];

	m_values[Jx] = oldJx * cosTheta + oldKx * sinTheta;
	m_values[Jy] = oldJy * cosTheta + oldKy * sinTheta;
	m_values[Jz] = oldJz * cosTheta + oldKz * sinTheta;
	m_values[Jw] = oldJw * cosTheta + oldKw * sinTheta;

	m_values[Kx] = oldKx * cosTheta - oldJx * sinTheta;
	m_values[Ky] = oldKy * cosTheta - oldJy * sinTheta;
	m_values[Kz] = oldKz * cosTheta - oldJz * sinTheta;
	m_values[Kw] = oldKw * cosTheta - oldJw * sinTheta;
}

void Mat44::AppendTranslation2D(Vec2 const& translationXY)
{
    //same as appending (*= in column notation) a translation matrix
	/*Mat44 translation = MakeTranslation2D(translationXY);
	Multiply(translation);*/
	float tx = translationXY.x;
    float ty = translationXY.y;

    m_values[Tx] += m_values[Ix] * tx + m_values[Jx] * ty;
    m_values[Ty] += m_values[Iy] * tx + m_values[Jy] * ty;
    m_values[Tz] += m_values[Iz] * tx + m_values[Jz] * ty;
    m_values[Tw] += m_values[Iw] * tx + m_values[Jw] * ty;
}

void Mat44::AppendTranslation3D(Vec3 const& translationXYZ)
{
   //same as appending (*= in column notation) a translation matrix
	/*Mat44 translation = MakeTranslation3D(translationXYZ);
	Append(translation);*/
	float tx = translationXYZ.x;
	float ty = translationXYZ.y;
	float tz = translationXYZ.z;

	m_values[Tx] += m_values[Ix] * tx + m_values[Jx] * ty + m_values[Kx] * tz;
	m_values[Ty] += m_values[Iy] * tx + m_values[Jy] * ty + m_values[Ky] * tz;
	m_values[Tz] += m_values[Iz] * tx + m_values[Jz] * ty + m_values[Kz] * tz;
	m_values[Tw] += m_values[Iw] * tx + m_values[Jw] * ty + m_values[Kw] * tz;
}

void Mat44::AppendScaleUniform2D(float uniformScaleXY)
{
    //K and T bases should remain unaffected
	m_values[Ix] *= uniformScaleXY;
	m_values[Iy] *= uniformScaleXY;
	m_values[Iw] *= uniformScaleXY;
	m_values[Iz] *= uniformScaleXY;

	m_values[Jx] *= uniformScaleXY;
	m_values[Jy] *= uniformScaleXY;
	m_values[Jw] *= uniformScaleXY;
	m_values[Jz] *= uniformScaleXY;
}

void Mat44::AppendScaleUniform3D(float uniformScaleXYZ)
{
    //translation should remain unaffected
	m_values[Ix] *= uniformScaleXYZ;
	m_values[Iy] *= uniformScaleXYZ;
	m_values[Iz] *= uniformScaleXYZ;
	m_values[Iw] *= uniformScaleXYZ;

	m_values[Jx] *= uniformScaleXYZ;
	m_values[Jy] *= uniformScaleXYZ;
	m_values[Jz] *= uniformScaleXYZ;
	m_values[Jw] *= uniformScaleXYZ;

	m_values[Kx] *= uniformScaleXYZ;
	m_values[Ky] *= uniformScaleXYZ;
	m_values[Kz] *= uniformScaleXYZ;
	m_values[Kw] *= uniformScaleXYZ;
}

void Mat44::AppendScaleNonUniform2D(Vec2 const& nonUniformScaleXY)
{
    //K and T bases should remain unaffected
	m_values[Ix] *= nonUniformScaleXY.x;
	m_values[Iy] *= nonUniformScaleXY.x;
	m_values[Iw] *= nonUniformScaleXY.x;
	m_values[Iz] *= nonUniformScaleXY.x;

	m_values[Jx] *= nonUniformScaleXY.y;
	m_values[Jy] *= nonUniformScaleXY.y;
	m_values[Jw] *= nonUniformScaleXY.y;
	m_values[Jz] *= nonUniformScaleXY.y;
}

void Mat44::AppendScaleNonUniform3D(Vec3 const& nonUniformScaleXYZ)
{
    //translation should remain unaffected
	m_values[Ix] *= nonUniformScaleXYZ.x;
	m_values[Iy] *= nonUniformScaleXYZ.x;
	m_values[Iz] *= nonUniformScaleXYZ.x;
	m_values[Iw] *= nonUniformScaleXYZ.x;

	m_values[Jx] *= nonUniformScaleXYZ.y;
	m_values[Jy] *= nonUniformScaleXYZ.y;
	m_values[Jz] *= nonUniformScaleXYZ.y;
	m_values[Jw] *= nonUniformScaleXYZ.y;

	m_values[Kx] *= nonUniformScaleXYZ.z;
	m_values[Ky] *= nonUniformScaleXYZ.z;
	m_values[Kz] *= nonUniformScaleXYZ.z;
	m_values[Kw] *= nonUniformScaleXYZ.z;
}

Mat44 Mat44::operator*(const Mat44& rhs) const
{
	Mat44 result = rhs;
	result.Append(*this);  // this * rhs = Append this to rhs TODO:到底有没有问题啊
	return result;
}


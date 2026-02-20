#include "ParticleSystem.h"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Texture.hpp"
#include "Engine/Renderer/SpriteSheet.hpp"
#include "Engine/Renderer/SpriteDefinition.hpp"
#include "Engine/Renderer/Camera.hpp"
#include "Engine/Math/RandomNumberGenerator.hpp"
#include "Engine/Math/MathUtils.hpp"

ParticleEmitter::ParticleEmitter(const ParticleEmitterConfig& config, int maxParticles)
	: m_config(config)
{
	m_particles.resize(maxParticles);
	m_vertexCache.reserve(maxParticles * 6); 
}

ParticleEmitter::~ParticleEmitter()
{
}

void ParticleEmitter::Update(float deltaSeconds)
{
	for (Particle& particle : m_particles)
	{
		if (particle.m_isAlive)
		{
			UpdateParticle(particle, deltaSeconds);
		}
	}
	
	// 发射新粒子
	if (m_isEmitting && m_config.m_emissionRate > 0.0f)
	{
		m_emissionAccumulator += deltaSeconds * m_config.m_emissionRate;
		
		while (m_emissionAccumulator >= 1.0f)
		{
			SpawnParticle();
			m_emissionAccumulator -= 1.0f;
		}
	}
}

void ParticleEmitter::UpdateParticle(Particle& particle, float deltaSeconds)
{
	// 更新年龄
	particle.m_age += deltaSeconds;
	if (particle.m_age >= particle.m_maxAge)
	{
		particle.m_isAlive = false;
		return;
	}
	
	particle.m_velocity += particle.m_acceleration * deltaSeconds;
	particle.m_position += particle.m_velocity * deltaSeconds;
	
	particle.m_rotation += particle.m_rotationSpeed * deltaSeconds;
	
	float t = particle.GetNormalizedAge();
	
	particle.m_currentColor = InterpolateRgba8(particle.m_startColor, particle.m_endColor, t);
	
	particle.m_currentSize = Interpolate(particle.m_startSize, particle.m_endSize, t);
	
	if (m_config.m_animateSprite && m_spriteSheet)
	{
		int spriteRange = m_config.m_spriteIndexEnd - m_config.m_spriteIndexStart + 1;
		int frameIndex = (int)(t * spriteRange);
		frameIndex = GetClampedInt(frameIndex, 0, spriteRange - 1);
		particle.m_spriteIndex = m_config.m_spriteIndexStart + frameIndex;
	}
}

void ParticleEmitter::Render(Renderer* renderer, const Camera& camera) const
{
	if (!renderer) return;
	
	// 清空顶点缓存
	m_vertexCache.clear();
	
	Vec3 camForward, camLeft, camUp;
	camera.GetOrientation().GetAsVectors_IFwd_JLeft_KUp(camForward, camLeft, camUp);
	Vec3 camRight = -camLeft; 
	
	// 为每个活着的粒子生成quad
	for (const Particle& particle : m_particles)
	{
		if (particle.m_isAlive)
		{
			AddParticleQuad(m_vertexCache, particle, camRight, camUp);
		}
	}
	// 渲染
	if (!m_vertexCache.empty())
	{
		if (m_texture)
		{
			renderer->BindTexture(m_texture);
		}
		else if (m_spriteSheet)
		{
			renderer->BindTexture(&m_spriteSheet->GetTexture());
		}
		
		renderer->DrawVertexArray(m_vertexCache);
	}
}

void ParticleEmitter::AddParticleQuad(std::vector<Vertex_PCU>& verts, const Particle& particle, const Vec3& camRight, const Vec3& camUp) const
{
	float halfSize = particle.m_currentSize * 0.5f;
	
	// 计算旋转后的right和up向量
	float rotRad = ConvertDegreesToRadians(particle.m_rotation);
	float cosR = cosf(rotRad);
	float sinR = sinf(rotRad);
	
	Vec3 right = camRight * cosR + camUp * sinR;
	Vec3 up = camUp * cosR - camRight * sinR;
	
	// 四个角的位置
	Vec3 bottomLeft  = particle.m_position - right * halfSize - up * halfSize;
	Vec3 bottomRight = particle.m_position + right * halfSize - up * halfSize;
	Vec3 topRight    = particle.m_position + right * halfSize + up * halfSize;
	Vec3 topLeft     = particle.m_position - right * halfSize + up * halfSize;
	
	// UV坐标
	Vec2 uvMins(0.0f, 0.0f);
	Vec2 uvMaxs(1.0f, 1.0f);
	
	if (m_spriteSheet)
	{
		AABB2 spriteUVs = m_spriteSheet->GetSpriteUVs(particle.m_spriteIndex);
		uvMins = spriteUVs.m_mins;
		uvMaxs = spriteUVs.m_maxs;
	}
	
	Rgba8 color = particle.m_currentColor;
	
	// 第一个三角形
	verts.push_back(Vertex_PCU(bottomLeft,  color, Vec2(uvMins.x, uvMins.y)));
	verts.push_back(Vertex_PCU(bottomRight, color, Vec2(uvMaxs.x, uvMins.y)));
	verts.push_back(Vertex_PCU(topRight,    color, Vec2(uvMaxs.x, uvMaxs.y)));
	
	// 第二个三角形
	verts.push_back(Vertex_PCU(bottomLeft,  color, Vec2(uvMins.x, uvMins.y)));
	verts.push_back(Vertex_PCU(topRight,    color, Vec2(uvMaxs.x, uvMaxs.y)));
	verts.push_back(Vertex_PCU(topLeft,     color, Vec2(uvMins.x, uvMaxs.y)));
}

void ParticleEmitter::SpawnParticle()
{
	int index = FindDeadParticleIndex();
	if (index < 0) return;
	
	Particle& p = m_particles[index];
	p.m_isAlive = true;
	p.m_age = 0.0f;
	
	// 位置
	p.m_position = m_config.m_spawnPosition;
	if (m_rng && m_config.m_spawnPositionVariance != Vec3())
	{
		p.m_position.x += m_rng->RollRandomFloatInRange(-m_config.m_spawnPositionVariance.x, m_config.m_spawnPositionVariance.x);
		p.m_position.y += m_rng->RollRandomFloatInRange(-m_config.m_spawnPositionVariance.y, m_config.m_spawnPositionVariance.y);
		p.m_position.z += m_rng->RollRandomFloatInRange(-m_config.m_spawnPositionVariance.z, m_config.m_spawnPositionVariance.z);
	}
	
	// 速度
	p.m_velocity = m_config.m_startVelocity;
	if (m_rng && m_config.m_velocityVariance != Vec3())
	{
		p.m_velocity.x += m_rng->RollRandomFloatInRange(-m_config.m_velocityVariance.x, m_config.m_velocityVariance.x);
		p.m_velocity.y += m_rng->RollRandomFloatInRange(-m_config.m_velocityVariance.y, m_config.m_velocityVariance.y);
		p.m_velocity.z += m_rng->RollRandomFloatInRange(-m_config.m_velocityVariance.z, m_config.m_velocityVariance.z);
	}
	
	// 加速度
	p.m_acceleration = m_config.m_acceleration;
	
	// 颜色
	p.m_startColor = m_config.m_startColor;
	p.m_endColor = m_config.m_endColor;
	if (m_rng && m_config.m_randomizeStartColor)
	{
		p.m_startColor.r = (unsigned char)m_rng->RollRandomIntInRange(0, 255);
		p.m_startColor.g = (unsigned char)m_rng->RollRandomIntInRange(0, 255);
		p.m_startColor.b = (unsigned char)m_rng->RollRandomIntInRange(0, 255);
	}
	p.m_currentColor = p.m_startColor;
	
	// 大小
	p.m_startSize = m_config.m_startSize;
	if (m_rng && m_config.m_startSizeVariance > 0.0f)
	{
		p.m_startSize += m_rng->RollRandomFloatInRange(-m_config.m_startSizeVariance, m_config.m_startSizeVariance);
		p.m_startSize = MaxF(p.m_startSize, 0.001f);
	}
	p.m_endSize = m_config.m_endSize;
	p.m_currentSize = p.m_startSize;
	
	// 生命周期
	if (m_rng)
	{
		p.m_maxAge = m_rng->RollRandomFloatInRange(m_config.m_minLifetime, m_config.m_maxLifetime);
	}
	else
	{
		p.m_maxAge = (m_config.m_minLifetime + m_config.m_maxLifetime) * 0.5f;
	}
	
	// 旋转
	p.m_rotation = m_config.m_startRotation;
	if (m_rng && m_config.m_startRotationVariance > 0.0f)
	{
		p.m_rotation += m_rng->RollRandomFloatInRange(-m_config.m_startRotationVariance, m_config.m_startRotationVariance);
	}
	p.m_rotationSpeed = m_config.m_rotationSpeed;
	if (m_rng && m_config.m_rotationSpeedVariance > 0.0f)
	{
		p.m_rotationSpeed += m_rng->RollRandomFloatInRange(-m_config.m_rotationSpeedVariance, m_config.m_rotationSpeedVariance);
	}
	
	// Sprite
	p.m_spriteIndex = m_config.m_spriteIndexStart;
	if (m_rng && m_config.m_randomSpriteIndex && m_config.m_spriteIndexEnd > m_config.m_spriteIndexStart)
	{
		p.m_spriteIndex = m_rng->RollRandomIntInRange(m_config.m_spriteIndexStart, m_config.m_spriteIndexEnd);
	}
}

int ParticleEmitter::FindDeadParticleIndex() const
{
	for (int i = 0; i < (int)m_particles.size(); ++i)
	{
		if (!m_particles[i].m_isAlive)
		{
			return i;
		}
	}
	return -1;  // 没有空闲粒子
}

void ParticleEmitter::Start()
{
	m_isEmitting = true;
}

void ParticleEmitter::Stop()
{
	m_isEmitting = false;
}

void ParticleEmitter::Reset()
{
	m_isEmitting = false;
	m_emissionAccumulator = 0.0f;
	for (Particle& p : m_particles)
	{
		p.m_isAlive = false;
	}
}

void ParticleEmitter::Burst(int count)
{
	int numToSpawn = (count < 0) ? m_config.m_burstCount : count;
	for (int i = 0; i < numToSpawn; ++i)
	{
		SpawnParticle();
	}
}

void ParticleEmitter::SetPosition(const Vec3& position)
{
	m_config.m_spawnPosition = position;
}

void ParticleEmitter::SetTexture(Texture* texture)
{
	m_texture = texture;
	m_spriteSheet = nullptr;
}

void ParticleEmitter::SetSpriteSheet(SpriteSheet* spriteSheet)
{
	m_spriteSheet = spriteSheet;
	m_texture = nullptr;
}

void ParticleEmitter::SetRNG(RandomNumberGenerator* rng)
{
	m_rng = rng;
}

int ParticleEmitter::GetAliveParticleCount() const
{
	int count = 0;
	for (const Particle& p : m_particles)
	{
		if (p.m_isAlive) ++count;
	}
	return count;
}

//===============================================================================================
// ParticleSystem
//===============================================================================================
ParticleSystem::ParticleSystem()
{
}

ParticleSystem::~ParticleSystem()
{
	Shutdown();
}

void ParticleSystem::Startup(Renderer* renderer)
{
	m_renderer = renderer;
}

void ParticleSystem::Shutdown()
{
	DestroyAllEmitters();
	m_renderer = nullptr;
}

void ParticleSystem::Update(float deltaSeconds)
{
	// 更新所有发射器
	for (ParticleEmitter* emitter : m_emitters)
	{
		if (emitter)
		{
			emitter->Update(deltaSeconds);
		}
	}
	
	// 移除不活跃的发射器（可选，根据需要启用）
	// auto it = std::remove_if(m_emitters.begin(), m_emitters.end(), 
	//     [](ParticleEmitter* e) { return e && !e->IsActive(); });
	// for (auto iter = it; iter != m_emitters.end(); ++iter)
	// {
	//     delete *iter;
	// }
	// m_emitters.erase(it, m_emitters.end());
}

void ParticleSystem::Render(const Camera& camera) const
{
	if (!m_renderer) return;
	
	for (ParticleEmitter* emitter : m_emitters)
	{
		if (emitter)
		{
			emitter->Render(m_renderer, camera);
		}
	}
}

ParticleEmitter* ParticleSystem::CreateEmitter(const ParticleEmitterConfig& config, int maxParticles)
{
	ParticleEmitter* emitter = new ParticleEmitter(config, maxParticles);
	
	if (m_defaultRNG)
	{
		emitter->SetRNG(m_defaultRNG);
	}
	if (m_defaultTexture)
	{
		emitter->SetTexture(m_defaultTexture);
	}
	
	m_emitters.push_back(emitter);
	return emitter;
}

void ParticleSystem::DestroyEmitter(ParticleEmitter* emitter)
{
	for (auto it = m_emitters.begin(); it != m_emitters.end(); ++it)
	{
		if (*it == emitter)
		{
			delete emitter;
			m_emitters.erase(it);
			return;
		}
	}
}

void ParticleSystem::DestroyAllEmitters()
{
	for (ParticleEmitter* emitter : m_emitters)
	{
		delete emitter;
	}
	m_emitters.clear();
}

//-----------------------------------------------------------------------------------------------
// 便捷效果函数
//-----------------------------------------------------------------------------------------------
ParticleEmitter* ParticleSystem::CreateFireEffect(const Vec3& position, float scale)
{
	ParticleEmitterConfig config;
	config.m_spawnPosition = position;
	config.m_spawnPositionVariance = Vec3(0.2f, 0.2f, 0.0f) * scale;
	
	config.m_startVelocity = Vec3(0.0f, 0.0f, 2.0f) * scale;
	config.m_velocityVariance = Vec3(0.5f, 0.5f, 0.5f) * scale;
	config.m_acceleration = Vec3(0.0f, 0.0f, 1.0f) * scale;  // 火焰向上
	
	config.m_startColor = Rgba8(255, 200, 50, 255);
	config.m_endColor = Rgba8(255, 50, 0, 0);
	
	config.m_startSize = 0.3f * scale;
	config.m_startSizeVariance = 0.1f * scale;
	config.m_endSize = 0.05f * scale;
	
	config.m_minLifetime = 0.3f;
	config.m_maxLifetime = 0.8f;
	
	config.m_emissionRate = 50.0f;
	
	ParticleEmitter* emitter = CreateEmitter(config, 500);
	emitter->Start();
	return emitter;
}

ParticleEmitter* ParticleSystem::CreateSmokeEffect(const Vec3& position, float scale)
{
	ParticleEmitterConfig config;
	config.m_spawnPosition = position;
	config.m_spawnPositionVariance = Vec3(0.1f, 0.1f, 0.0f) * scale;
	
	config.m_startVelocity = Vec3(0.0f, 0.0f, 1.0f) * scale;
	config.m_velocityVariance = Vec3(0.3f, 0.3f, 0.2f) * scale;
	config.m_acceleration = Vec3(0.2f, 0.0f, 0.5f) * scale;  // 轻微飘动
	
	config.m_startColor = Rgba8(80, 80, 80, 200);
	config.m_endColor = Rgba8(50, 50, 50, 0);
	
	config.m_startSize = 0.2f * scale;
	config.m_startSizeVariance = 0.05f * scale;
	config.m_endSize = 0.8f * scale;  // 烟雾扩散
	
	config.m_minLifetime = 1.0f;
	config.m_maxLifetime = 3.0f;
	
	config.m_emissionRate = 15.0f;
	
	ParticleEmitter* emitter = CreateEmitter(config, 200);
	emitter->Start();
	return emitter;
}

ParticleEmitter* ParticleSystem::CreateSparkEffect(const Vec3& position, float scale)
{
	ParticleEmitterConfig config;
	config.m_spawnPosition = position;
	config.m_spawnPositionVariance = Vec3(0.05f, 0.05f, 0.05f) * scale;
	
	config.m_startVelocity = Vec3(0.0f, 0.0f, 3.0f) * scale;
	config.m_velocityVariance = Vec3(2.0f, 2.0f, 2.0f) * scale;
	config.m_acceleration = Vec3(0.0f, 0.0f, -9.8f) * scale;  // 重力
	
	config.m_startColor = Rgba8(255, 255, 200, 255);
	config.m_endColor = Rgba8(255, 150, 50, 0);
	
	config.m_startSize = 0.05f * scale;
	config.m_endSize = 0.02f * scale;
	
	config.m_minLifetime = 0.2f;
	config.m_maxLifetime = 0.6f;
	
	config.m_emissionRate = 100.0f;
	
	ParticleEmitter* emitter = CreateEmitter(config, 300);
	emitter->Start();
	return emitter;
}

ParticleEmitter* ParticleSystem::CreateExplosionEffect(const Vec3& position, float scale)
{
	ParticleEmitterConfig config;
	config.m_spawnPosition = position;
	config.m_spawnPositionVariance = Vec3();
	
	// 爆炸向所有方向发射
	config.m_startVelocity = Vec3();
	config.m_velocityVariance = Vec3(5.0f, 5.0f, 5.0f) * scale;
	config.m_acceleration = Vec3(0.0f, 0.0f, -2.0f) * scale;
	
	config.m_startColor = Rgba8(255, 200, 100, 255);
	config.m_endColor = Rgba8(100, 50, 0, 0);
	
	config.m_startSize = 0.3f * scale;
	config.m_startSizeVariance = 0.1f * scale;
	config.m_endSize = 0.1f * scale;
	
	config.m_minLifetime = 0.3f;
	config.m_maxLifetime = 1.0f;
	
	config.m_burstCount = 100;  // 一次性发射
	config.m_emissionRate = 0.0f;
	
	ParticleEmitter* emitter = CreateEmitter(config, 150);
	emitter->Burst();  // 立即发射
	return emitter;
}

int ParticleSystem::GetTotalParticleCount() const
{
	int total = 0;
	for (const ParticleEmitter* emitter : m_emitters)
	{
		if (emitter)
		{
			total += emitter->GetAliveParticleCount();
		}
	}
	return total;
}

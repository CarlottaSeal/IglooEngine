#pragma once
#include "Engine/Math/Vec3.hpp"
#include "Engine/Core/Rgba8.hpp"
#include "Engine/Core/Vertex_PCU.hpp"
#include <vector>

class Renderer;
class Texture;
class SpriteSheet;
class RandomNumberGenerator;
class Camera;
class VertexBuffer;

struct Particle
{
	Vec3 m_position;
	Vec3 m_velocity;
	Vec3 m_acceleration;
	
	Rgba8 m_startColor = Rgba8::WHITE;
	Rgba8 m_endColor = Rgba8::WHITE;
	Rgba8 m_currentColor = Rgba8::WHITE;
	
	float m_startSize = 1.0f;
	float m_endSize = 1.0f;
	float m_currentSize = 1.0f;
	
	float m_age = 0.0f;
	float m_maxAge = 1.0f;
	
	float m_rotation = 0.0f;         // 当前旋转角度（degrees）
	float m_rotationSpeed = 0.0f;    // 旋转速度（degrees/sec）
	
	int m_spriteIndex = 0;           // 用于动画或随机sprite
	
	bool m_isAlive = false;
	
	float GetNormalizedAge() const { return m_age / m_maxAge; }
};

struct ParticleEmitterConfig
{
	// 发射位置/区域
	Vec3 m_spawnPosition = Vec3();
	Vec3 m_spawnPositionVariance = Vec3();  // 随机偏移范围
	
	// 发射速度
	Vec3 m_startVelocity = Vec3(0.f, 0.f, 1.f);
	Vec3 m_velocityVariance = Vec3();
	
	// 加速度（重力等）
	Vec3 m_acceleration = Vec3(0.f, 0.f, -9.8f);
	
	// 颜色
	Rgba8 m_startColor = Rgba8::WHITE;
	Rgba8 m_endColor = Rgba8(255, 255, 255, 0);  // 默认淡出
	bool m_randomizeStartColor = false;
	
	// 大小
	float m_startSize = 0.1f;
	float m_startSizeVariance = 0.0f;
	float m_endSize = 0.0f;
	
	// 生命周期
	float m_minLifetime = 0.5f;
	float m_maxLifetime = 2.0f;
	
	// 旋转
	float m_startRotation = 0.0f;
	float m_startRotationVariance = 0.0f;
	float m_rotationSpeed = 0.0f;
	float m_rotationSpeedVariance = 0.0f;
	
	// 发射速率
	float m_emissionRate = 10.0f;  // 每秒发射粒子数
	int m_burstCount = 0;          // 一次性发射数量（0表示连续发射）
	
	// Sprite动画
	int m_spriteIndexStart = 0;
	int m_spriteIndexEnd = 0;      // 如果相同则不做动画
	bool m_animateSprite = false;
	bool m_randomSpriteIndex = false;
};

class ParticleEmitter
{
public:
	ParticleEmitter(const ParticleEmitterConfig& config, int maxParticles = 1000);
	~ParticleEmitter();
	
	void Update(float deltaSeconds);
	void Render(Renderer* renderer, const Camera& camera) const;
	
	void Start();
	void Stop();
	void Reset();
	void Burst(int count = -1);  // -1 使用config中的burstCount
	
	// 设置
	void SetPosition(const Vec3& position);
	void SetTexture(Texture* texture);
	void SetSpriteSheet(SpriteSheet* spriteSheet);
	void SetRNG(RandomNumberGenerator* rng);
	
	// 查询
	bool IsActive() const { return m_isEmitting || GetAliveParticleCount() > 0; }
	int GetAliveParticleCount() const;
	const ParticleEmitterConfig& GetConfig() const { return m_config; }
	ParticleEmitterConfig& GetConfigMutable() { return m_config; }
	
private:
	void SpawnParticle();
	void UpdateParticle(Particle& particle, float deltaSeconds);
	void AddParticleQuad(std::vector<Vertex_PCU>& verts, const Particle& particle, const Vec3& camRight, const Vec3& camUp) const;
	int FindDeadParticleIndex() const;
	
private:
	ParticleEmitterConfig m_config;
	std::vector<Particle> m_particles;
	
	Texture* m_texture = nullptr;
	SpriteSheet* m_spriteSheet = nullptr;
	RandomNumberGenerator* m_rng = nullptr;
	
	float m_emissionAccumulator = 0.0f;
	bool m_isEmitting = false;
	
	// 用于渲染的顶点缓存
	mutable std::vector<Vertex_PCU> m_vertexCache;
};

class ParticleSystem
{
public:
	ParticleSystem();
	~ParticleSystem();
	
	void Startup(Renderer* renderer);
	void Shutdown();
	
	void Update(float deltaSeconds);
	void Render(const Camera& camera) const;
	
	// 发射器管理
	ParticleEmitter* CreateEmitter(const ParticleEmitterConfig& config, int maxParticles = 1000);
	void DestroyEmitter(ParticleEmitter* emitter);
	void DestroyAllEmitters();
	
	// 便捷函数 - 快速创建常见效果
	ParticleEmitter* CreateFireEffect(const Vec3& position, float scale = 1.0f);
	ParticleEmitter* CreateSmokeEffect(const Vec3& position, float scale = 1.0f);
	ParticleEmitter* CreateSparkEffect(const Vec3& position, float scale = 1.0f);
	ParticleEmitter* CreateExplosionEffect(const Vec3& position, float scale = 1.0f);
	
	// 设置
	void SetDefaultRNG(RandomNumberGenerator* rng) { m_defaultRNG = rng; }
	void SetDefaultTexture(Texture* texture) { m_defaultTexture = texture; }
	
	// 查询
	int GetTotalParticleCount() const;
	int GetEmitterCount() const { return (int)m_emitters.size(); }
	
private:
	Renderer* m_renderer = nullptr;
	RandomNumberGenerator* m_defaultRNG = nullptr;
	Texture* m_defaultTexture = nullptr;
	
	std::vector<ParticleEmitter*> m_emitters;
};

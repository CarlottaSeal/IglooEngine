#include "DebugRenderSystem.hpp"

#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/BitmapFont.hpp"
#include "Engine/Core/EngineCommon.hpp"
#include "Engine/Core/Timer.hpp"
#include "Engine/Core/VertexUtils.hpp"
#include "Engine/Core/Vertex_PCU.hpp"
#include "Engine/Window/Window.hpp"
#include "Engine/Math/MathUtils.hpp"

extern Window* g_theWindow;


class DebugObject
{
public:
    DebugObject(float duration, const Rgba8& startColor = Rgba8::WHITE, const Rgba8& endColor = Rgba8::WHITE, DebugRenderMode mode = DebugRenderMode::USE_DEPTH);
    void UpdateWorldObjects();
    void UpdateScreenObjects();
    void RenderWorldObjects(Camera const& camera, Renderer* renderer) const;
	void RenderScreenObjects(Camera const& camera, Renderer* renderer) const;

public:
    std::vector<Vertex_PCU> m_verts;
    Timer* m_timer = nullptr;
    DebugRenderMode m_mode = DebugRenderMode::ALWAYS;
    BitmapFont* m_textFont = nullptr;
    Vec3 m_position;
    float m_duration;
    Rgba8 m_startColor;
    Rgba8 m_endColor;
    Rgba8 m_lerpColor;
    bool m_hasEndedLife = false;
    bool m_isWireFrame = false;
    bool m_isBillboard = false;
    bool m_isMessage = false;
    std::string m_message;
    bool m_isText = false;
};

DebugObject::DebugObject(float duration, const Rgba8& startColor, const Rgba8& endColor, DebugRenderMode mode)
    :m_duration(duration)
    ,m_startColor(startColor)
    ,m_endColor(endColor)
    ,m_mode(mode)
    ,m_timer(nullptr)
{
    if (!m_timer)
    {
        m_timer = new Timer(duration);
    }
    else
    {
        m_timer->Start();
    }
}

void DebugObject::UpdateWorldObjects()
{
    float elapsedFraction = m_timer->GetElapsedFraction();
    m_lerpColor = InterpolateRgba8(m_startColor, m_endColor, elapsedFraction);
	for (Vertex_PCU& vertex : m_verts)
	{
		vertex.m_color = m_lerpColor;
	}
    if (m_duration == -1.f)
    {
        return;
    }
    if (m_timer->HasPeriodElapsed() || m_duration == 0.f )
    {
        m_timer->Stop();
        m_verts.clear();
        m_hasEndedLife = true;
    }
}

void DebugObject::UpdateScreenObjects()
{
	//int currentMessageCount = (int)g_theDebugRenderTool->m_messageObjects.size();
	//int removedCount = g_theDebugRenderTool->m_prevMessageCount - currentMessageCount;
 //   if (m_isText == true)
 //   {

 //   }
    if (m_timer->HasPeriodElapsed())
    {
        m_verts.clear();
        m_timer->Stop();
        m_hasEndedLife = true;
    }
    //g_theDebugRenderTool->m_prevMessageCount = currentMessageCount;
}

void DebugObject::RenderWorldObjects(Camera const& camera, Renderer* renderer) const
{
#ifdef ENGINE_DX11_RENDERER
    renderer->BindTexture(nullptr);
#endif
#ifdef ENGINE_DX12_RENDERER
	renderer->SetMaterialConstants();
#endif
    renderer->SetBlendMode(BlendMode::ALPHA);
    renderer->BindShader(nullptr);
    
    if (m_mode == DebugRenderMode::USE_DEPTH)
    {
        if (m_isWireFrame)
        {
            if (m_isText)
            {
                renderer->SetRasterizerMode(RasterizerMode::WIREFRAME_CULL_NONE);
            }
            else
                renderer->SetRasterizerMode(RasterizerMode::WIREFRAME_CULL_BACK);
        }
        else
        {
            if (m_isText)
            {
#ifdef ENGINE_DX11_RENDERER
                renderer->BindTexture(&m_textFont->GetTexture());
#endif
#ifdef ENGINE_DX12_RENDERER
            	renderer->SetMaterialConstants(&m_textFont->GetTexture());
#endif
                renderer->SetRasterizerMode(RasterizerMode::SOLID_CULL_NONE);
            }
            else
                renderer->SetRasterizerMode(RasterizerMode::SOLID_CULL_BACK);
        }

        if (m_isBillboard)
        {
			Mat44 cameraMat = camera.GetOrientation().GetAsMatrix_IFwd_JLeft_KUp();
	        cameraMat.SetTranslation3D(camera.GetPosition());
	        Mat44 mat = GetBillboardMatrix(BillboardType::FULL_OPPOSING, cameraMat, m_position);
			renderer->SetModelConstants(mat);
        }
        else
        {
            renderer->SetModelConstants();
        }

        renderer->SetDepthMode(DepthMode::READ_WRITE_LESS_EQUAL);
        renderer->DrawVertexArray(m_verts);
    }
	if (m_mode == DebugRenderMode::X_RAY)
	{
		if (m_isWireFrame)
		{
            if (m_isText)
            {
#ifdef ENGINE_DX11_RENDERER
                renderer->BindTexture(&m_textFont->GetTexture());
#endif
            	#ifdef ENGINE_DX12_RENDERER
            	renderer->SetMaterialConstants(&m_textFont->GetTexture());
            	#endif
                renderer->SetRasterizerMode(RasterizerMode::WIREFRAME_CULL_NONE);
            }
            else
			    renderer->SetRasterizerMode(RasterizerMode::WIREFRAME_CULL_BACK);
		}
		else
		{
            if (m_isText)
            {
				#ifdef ENGINE_DX11_RENDERER
                renderer->BindTexture(&m_textFont->GetTexture());
				#endif
            	#ifdef ENGINE_DX12_RENDERER
            	renderer->SetMaterialConstants(&m_textFont->GetTexture());
            	#endif
                renderer->SetRasterizerMode(RasterizerMode::SOLID_CULL_NONE);
            }
            else
			    renderer->SetRasterizerMode(RasterizerMode::SOLID_CULL_BACK);
		}

        if (m_isBillboard)
        {
			Mat44 cameraMat = camera.GetOrientation().GetAsMatrix_IFwd_JLeft_KUp();
			cameraMat.SetTranslation3D(camera.GetPosition());
			Mat44 mat = GetBillboardMatrix(BillboardType::FULL_OPPOSING, cameraMat, m_position);  //?Is that okay?

			renderer->SetBlendMode(BlendMode::ALPHA);
			renderer->SetDepthMode(DepthMode::DISABLED);
			Rgba8 lightColor = m_lerpColor;
			lightColor.a = 50;
			renderer->SetModelConstants(mat, lightColor);
			renderer->DrawVertexArray(m_verts);

			renderer->SetBlendMode(BlendMode::OPAQUE);
			renderer->SetDepthMode(DepthMode::DISABLED);
			renderer->SetModelConstants(mat, m_startColor);
			renderer->DrawVertexArray(m_verts);
        }
        else
        {
			renderer->SetBlendMode(BlendMode::ALPHA);
			renderer->SetDepthMode(DepthMode::READ_ONLY_ALWAYS);
			Rgba8 lightColor = m_lerpColor;
			lightColor.a = 50;
			renderer->SetModelConstants(Mat44(), lightColor);
			renderer->DrawVertexArray(m_verts);

			renderer->SetBlendMode(BlendMode::OPAQUE);
			renderer->SetDepthMode(DepthMode::READ_WRITE_LESS_EQUAL);
			renderer->SetModelConstants(Mat44(), m_startColor);
			renderer->DrawVertexArray(m_verts);
        }	
	}
    if (m_mode == DebugRenderMode::ALWAYS)
    {
        if (m_isBillboard)
        {
			Mat44 cameraMat = camera.GetOrientation().GetAsMatrix_IFwd_JLeft_KUp();
			cameraMat.SetTranslation3D(camera.GetPosition());
			Mat44 mat = GetBillboardMatrix(BillboardType::FULL_OPPOSING, cameraMat, m_position);
			renderer->SetModelConstants(mat);
        }
        /*else
        {
            renderer->SetModelConstants();
		}*/
        if (m_isText)
        {
			#ifdef ENGINE_DX11_RENDERER
            renderer->BindTexture(&m_textFont->GetTexture());
			#endif
        	#ifdef ENGINE_DX12_RENDERER
        	renderer->SetMaterialConstants(&m_textFont->GetTexture());
        	#endif
            renderer->SetRasterizerMode(RasterizerMode::SOLID_CULL_BACK);
        }
        else
		    renderer->SetRasterizerMode(RasterizerMode::SOLID_CULL_BACK);

        renderer->SetModelConstants();
        renderer->SetDepthMode(DepthMode::DISABLED);
        renderer->SetBlendMode(BlendMode::ALPHA);
        renderer->DrawVertexArray(m_verts);
    }

	renderer->SetModelConstants();
#ifdef ENGINE_DX11_RENDERER
	renderer->BindTexture(nullptr);
#endif
#ifdef ENGINE_DX12_RENDERER
	renderer->SetMaterialConstants();
#endif
	renderer->SetRasterizerMode(RasterizerMode::SOLID_CULL_BACK);
	renderer->SetDepthMode(DepthMode::READ_WRITE_LESS_EQUAL);
	renderer->SetBlendMode(BlendMode::ALPHA);
}

void DebugObject::RenderScreenObjects(Camera const& camera, Renderer* renderer) const
{
    UNUSED(camera)
	#ifdef ENGINE_DX11_RENDERER
    renderer->BindTexture(&m_textFont->GetTexture());
	#endif
	#ifdef ENGINE_DX12_RENDERER
	renderer->SetMaterialConstants(&m_textFont->GetTexture());
	#endif
	renderer->BindShader(nullptr);
	renderer->SetModelConstants();
    //Mat44 cameraMat = camera.GetOrientation().GetAsMatrix_IFwd_JLeft_KUp();
    //cameraMat.SetTranslation3D(camera.GetPosition());
    //Mat44 mat = GetBillboardMatrix(BillboardType::FULL_OPPOSING, cameraMat, m_position);
    renderer->SetRasterizerMode(RasterizerMode::SOLID_CULL_NONE);
    
    //renderer->SetModelConstants(mat);
    renderer->DrawVertexArray(m_verts);
}

class DebugRenderTool
{
public:
    DebugRenderTool(DebugRenderConfig const& debugRenderConfig);
    ~DebugRenderTool();

    void Update();
    void RenderWorld(Camera const& worldCamera);
    void RenderScreen(Camera const& screenCamera);

public:
	//mutable std::mutex m_mutex;
	mutable std::recursive_mutex m_mutex;
    std::vector<DebugObject*> m_worldObjects;
    std::vector<DebugObject*> m_screenObjects;

	std::vector<DebugObject*> m_messageObjects;

    DebugRenderConfig m_debugRenderConfig;

    bool m_isHidden = false;

    float m_messageLineHeight;
	float m_messageStartX;
    float m_windowHeight;
};

DebugRenderTool* g_theDebugRenderTool = nullptr;

DebugRenderTool::DebugRenderTool(DebugRenderConfig const& debugRenderConfig)
{
    m_debugRenderConfig.m_renderer = debugRenderConfig.m_renderer;
    m_debugRenderConfig.m_fontName = debugRenderConfig.m_fontName;

    g_theEventSystem->SubscribeEventCallBackFunction("cleardebugobjects", Command_DebugRenderClear);
	g_theEventSystem->SubscribeEventCallBackFunction("toggledebugmode", Command_DebugRenderToggle);
}

DebugRenderTool::~DebugRenderTool()
{
	for (DebugObject* object : m_worldObjects)
	{
		delete object;
		object = nullptr;
	}
	for (DebugObject* object : m_screenObjects)
	{
		delete object;
		object = nullptr;
	}
	m_screenObjects.clear();
	m_worldObjects.clear();
}

void DebugRenderTool::Update()
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	
	for (DebugObject* object : m_worldObjects)
	{
        object->UpdateWorldObjects();
	}

	for (DebugObject* object : m_screenObjects)
	{
        object->UpdateScreenObjects();
	}

    float currentY = m_windowHeight-m_messageLineHeight;
	for (int i = 0; i < m_messageObjects.size(); i++)
	{
        DebugObject* message = m_messageObjects[i];

		message->m_verts.clear();

		message->m_textFont->AddVertsForText2D(
			message->m_verts, Vec2(m_messageStartX, currentY), m_messageLineHeight*0.7f, message->m_message, message->m_startColor
		);

		currentY -= m_messageLineHeight;
	}
}

void DebugRenderTool::RenderWorld(Camera const& worldCamera)
{
    if (!m_isHidden)
    {
    	std::lock_guard<std::recursive_mutex> lock(m_mutex);

#ifdef ENGINE_DX12_RENDERER
        m_debugRenderConfig.m_renderer->SetRenderMode(RenderMode::FORWARD);
#endif
		m_debugRenderConfig.m_renderer->BeginCamera(worldCamera);
		for (DebugObject* object : m_worldObjects)
		{
			object->RenderWorldObjects(worldCamera, m_debugRenderConfig.m_renderer);
		}
		m_debugRenderConfig.m_renderer->EndCamera(worldCamera);
    }  
}

void DebugRenderTool::RenderScreen(Camera const& screenCamera)
{
    if (!m_isHidden)
    {
#ifdef ENGINE_DX12_RENDERER
		m_debugRenderConfig.m_renderer->SetRenderMode(RenderMode::FORWARD);
#endif
        m_debugRenderConfig.m_renderer->BeginCamera(screenCamera);
        for (DebugObject* object : m_screenObjects)
        {
            object->RenderScreenObjects(screenCamera, m_debugRenderConfig.m_renderer);
        }
        m_debugRenderConfig.m_renderer->EndCamera(screenCamera);
    }
}

//-----------------------------------------------------------------------------------------------------------------------------

void DebugRenderSystemStartup(const DebugRenderConfig& config)
{
    if (!g_theDebugRenderTool)
    {
        g_theDebugRenderTool = new DebugRenderTool(config);
    }
}

void DebugRenderSystemShutdown()
{
	/*for (DebugObject* object : g_theDebugObjects)
	{
		delete object;
		object = nullptr;
	}
    g_theDebugObjects.clear();*/
    if (g_theDebugRenderTool)
    {
        delete g_theDebugRenderTool;
        g_theDebugRenderTool = nullptr;
    }
}

void DebugRenderSetVisible()
{
    if (g_theDebugRenderTool)
    {
        g_theDebugRenderTool->m_isHidden = false;
    }
}

void DebugRenderSetHidden()
{
	if (g_theDebugRenderTool)
	{
		g_theDebugRenderTool->m_isHidden = true;
	}
}

void DebugRenderClear()
{
    if (g_theDebugRenderTool)
    {
    	std::lock_guard<std::recursive_mutex> lock(g_theDebugRenderTool->m_mutex);
    	
        for (DebugObject* object : g_theDebugRenderTool->m_worldObjects)
        {
            delete object;
            object = nullptr;
        }
		for (DebugObject* object : g_theDebugRenderTool->m_screenObjects)
		{
			delete object;
			object = nullptr;
		}
		
        g_theDebugRenderTool->m_screenObjects.clear();
        g_theDebugRenderTool->m_worldObjects.clear();
        g_theDebugRenderTool->m_messageObjects.clear();
    }
}

void DebugRenderBeginFrame()
{
    if (g_theDebugRenderTool)
    {
        g_theDebugRenderTool->Update();
    }
}

void DebugRenderWorld(const Camera& camera)
{
    if (g_theDebugRenderTool)
    {
        g_theDebugRenderTool->RenderWorld(camera);
    }
}

void DebugRenderScreen(const Camera& camera)
{
	if (g_theDebugRenderTool)
	{
		g_theDebugRenderTool->RenderScreen(camera);
	}
}

void DebugRenderEndFrame()
{
    if (g_theDebugRenderTool)
    {
    	std::lock_guard<std::recursive_mutex> lock(g_theDebugRenderTool->m_mutex);
    	
		auto& worldObjects = g_theDebugRenderTool->m_worldObjects;

		for (auto iter = worldObjects.begin(); iter != worldObjects.end();)
		{
			DebugObject* object = *iter;
			if (object->m_hasEndedLife)
			{
				iter = worldObjects.erase(iter);
				delete object;
                object = nullptr;
			}
			else
			{
				++iter;
			}
		}
		auto& screenObjects = g_theDebugRenderTool->m_screenObjects;
        auto& messageObjects = g_theDebugRenderTool->m_messageObjects;

		for (auto iter = screenObjects.begin(); iter != screenObjects.end();)
		{
			DebugObject* object = *iter;
			if (object->m_hasEndedLife)
			{
				iter = screenObjects.erase(iter);

				if (object->m_isMessage)
				{			
					auto msgIter = std::find(messageObjects.begin(), messageObjects.end(), object);
					if (msgIter != messageObjects.end())
					{
						messageObjects.erase(msgIter);
					}
				}

				delete object;
				object = nullptr;
			}
			else
			{
				++iter;
			}
		}
    }
}

//Drawing functions
void DebugAddWorldPoint(const Vec3& pos, float radius, float duration, const Rgba8& startColor, const Rgba8& endColor, DebugRenderMode mode)
{
    DebugObject* point = new DebugObject(duration, startColor, endColor, mode);
    AddVertsForSphere3D(point->m_verts, pos, radius, startColor);
    {
    	std::lock_guard<std::recursive_mutex> lock(g_theDebugRenderTool->m_mutex);
    	g_theDebugRenderTool->m_worldObjects.push_back(point);
    }
    point->m_timer->Start();
}

void DebugAddWorldLine(const Vec3& start, const Vec3& end, float radius, float duration, const Rgba8& startColor, const Rgba8& endColor, DebugRenderMode mode)
{
	DebugObject* line = new DebugObject(duration, startColor, endColor, mode);
    AddVertsForCylinder3D(line->m_verts, start, end, radius);
	{
		std::lock_guard<std::recursive_mutex> lock(g_theDebugRenderTool->m_mutex);
		g_theDebugRenderTool->m_worldObjects.push_back(line);
	}
    line->m_timer->Start();
}

void DebugAddWorldWireCylinder(const Vec3& base, const Vec3& top, float radius, float duration, const Rgba8& startColor, const Rgba8& endColor, DebugRenderMode mode)
{
    DebugObject* wireCylinder = new DebugObject(duration, startColor, endColor, mode);
    AddVertsForCylinder3D(wireCylinder->m_verts, base, top, radius);
    wireCylinder->m_isWireFrame = true;
	{
		std::lock_guard<std::recursive_mutex> lock(g_theDebugRenderTool->m_mutex);
    	g_theDebugRenderTool->m_worldObjects.push_back(wireCylinder);
	}
    wireCylinder->m_timer->Start();
}

void DebugAddWorldWireSphere(const Vec3& center, float radius, float duration, const Rgba8& startColor, const Rgba8& endColor, DebugRenderMode mode)
{
    DebugObject* wireSphere = new DebugObject(duration, startColor, endColor, mode);
    AddVertsForSphere3D(wireSphere->m_verts, center, radius);
    wireSphere->m_isWireFrame = true;
	{
		std::lock_guard<std::recursive_mutex> lock(g_theDebugRenderTool->m_mutex);
    	g_theDebugRenderTool->m_worldObjects.push_back(wireSphere);
	}
    wireSphere->m_timer->Start();
}

void DebugAddWorldWireAABB(const AABB3& box, float duration, const Rgba8& startColor, const Rgba8& endColor,
	DebugRenderMode mode)
{
	DebugObject* wireBox = new DebugObject(duration, startColor, endColor, mode);
	AddVertsForAABB3D(wireBox->m_verts, box);
	wireBox->m_isWireFrame = true;
	{
		std::lock_guard<std::recursive_mutex> lock(g_theDebugRenderTool->m_mutex);
		g_theDebugRenderTool->m_worldObjects.push_back(wireBox);
	}
	wireBox->m_timer->Start();
}

void DebugAddWorldQuad(const Vec3& bl, const Vec3& br, const Vec3& tr, const Vec3& tl,
	float duration, const Rgba8& color, DebugRenderMode mode)
{
	DebugObject* quad = new DebugObject(duration, color, color, mode);
	AddVertsForQuad3D(quad->m_verts, bl, br, tr, tl, color);
	{
		std::lock_guard<std::recursive_mutex> lock(g_theDebugRenderTool->m_mutex);
		g_theDebugRenderTool->m_worldObjects.push_back(quad);
	}
	quad->m_timer->Start();
}

void DebugAddWorldArrow(const Vec3& start, const Vec3& end, float radius, float duration, const Rgba8& startColor, const Rgba8& endColor, DebugRenderMode mode)
{
    DebugObject* arrow = new DebugObject(duration, startColor, endColor, mode);
    AddVertsForArrow3D(arrow->m_verts, start, end, radius, startColor);
	{
		std::lock_guard<std::recursive_mutex> lock(g_theDebugRenderTool->m_mutex);
    	g_theDebugRenderTool->m_worldObjects.push_back(arrow);
	}
    arrow->m_timer->Start();
}

void DebugAddWorldText(const std::string& text, const Mat44& transform, float textHeight, const Vec2& alignment, float duration, const Rgba8& startColor, const Rgba8& endColor, DebugRenderMode mode)
{
	if (!g_theDebugRenderTool || !g_theDebugRenderTool->m_debugRenderConfig.m_renderer)
	{
		return;
	}

	DebugObject* textToWrite = new DebugObject(duration, startColor, endColor, mode);
    textToWrite->m_isText = true;
	textToWrite->m_textFont = g_theDebugRenderTool->m_debugRenderConfig.m_renderer->CreateOrGetBitmapFont(("Data/Fonts/" + g_theDebugRenderTool->m_debugRenderConfig.m_fontName).c_str());

	if (!textToWrite->m_textFont)
	{
		delete textToWrite;
		return;
	}

	textToWrite->m_textFont->AddVertsForText3DAtOriginXForward(textToWrite->m_verts, textHeight, text, startColor, 1.f, alignment);

	TransformVertexArray3D(textToWrite->m_verts, transform);
	{
		std::lock_guard<std::recursive_mutex> lock(g_theDebugRenderTool->m_mutex);
		g_theDebugRenderTool->m_worldObjects.push_back(textToWrite);
	}
	textToWrite->m_timer->Start();
}

void DebugAddWorldBillboardText(const std::string& text, const Vec3& origin, float textHeight, const Vec2& alignment, float duration, const Rgba8& startColor, DebugRenderMode mode)
{
	if (!g_theDebugRenderTool || !g_theDebugRenderTool->m_debugRenderConfig.m_renderer)
	{
		return;
	}

	DebugObject* textToWrite = new DebugObject(duration, startColor, startColor, mode);
    textToWrite->m_isText = true;
    textToWrite->m_isBillboard = true;

	textToWrite->m_textFont = g_theDebugRenderTool->m_debugRenderConfig.m_renderer->CreateOrGetBitmapFont(("Data/Fonts/" + g_theDebugRenderTool->m_debugRenderConfig.m_fontName).c_str());

	if (!textToWrite->m_textFont)
	{
		delete textToWrite;
		return;
	}

	textToWrite->m_textFont->AddVertsForText3DAtOriginXForward(textToWrite->m_verts, textHeight, text, startColor, 1.f, alignment);
    
    textToWrite->m_position = origin;
	/*Mat44 mat;
	mat.SetTranslation3D(origin);*/
    //TransformVertexArray3D(textToWrite->m_verts, mat); //transform the verts' position so render() doesn't transform...??
	{
		std::lock_guard<std::recursive_mutex> lock(g_theDebugRenderTool->m_mutex);
		g_theDebugRenderTool->m_worldObjects.push_back(textToWrite);
	}
	textToWrite->m_timer->Start();               
}

void DebugAddWorldBasis(const Mat44& transform, float duration, DebugRenderMode mode)
{
	Vec3 worldI = transform.GetIBasis3D().GetNormalized();
	Vec3 worldJ = transform.GetJBasis3D().GetNormalized();
	Vec3 worldK = transform.GetKBasis3D().GetNormalized();
    Vec3 worldT = transform.GetTranslation3D();
    /*DebugAddWorldArrow(Vec3(0.f, 0.f, 0.f), 1.5f*worldI, 0.15f, duration, Rgba8::AQUA, Rgba8::AQUA, mode);
    DebugAddWorldArrow(Vec3(0.f, 0.f, 0.f), -1.5f * worldJ, 0.15f, duration, Rgba8::MAGENTA, Rgba8::MAGENTA, mode);
    DebugAddWorldArrow(Vec3(0.f, 0.f, 0.f), 1.5f * worldK, 0.15f, duration, Rgba8::MINTGREEN, Rgba8::MINTGREEN, mode);*/
	DebugAddWorldArrow(worldT, worldT + 1.5f * worldI, 0.1f, duration, Rgba8::AQUA, Rgba8::AQUA, mode);
	DebugAddWorldArrow(worldT, worldT - 1.5f * worldJ, 0.1f, duration, Rgba8::MAGENTA, Rgba8::MAGENTA, mode);
	DebugAddWorldArrow(worldT, worldT + 1.5f * worldK, 0.1f, duration, Rgba8::MINTGREEN, Rgba8::MINTGREEN, mode);
}

void DebugAddScreenText(const std::string& text, const Vec2& position, float size, const Vec2& alignment, float duration, const Rgba8& startColor, const Rgba8& endColor)
{
    UNUSED(alignment);
	UNUSED(endColor);

	if (!g_theDebugRenderTool || !g_theDebugRenderTool->m_debugRenderConfig.m_renderer)
	{
		return;
	}

    DebugObject* textToWrite = new DebugObject(duration);
    textToWrite->m_isText = true;
    textToWrite->m_textFont = g_theDebugRenderTool->m_debugRenderConfig.m_renderer->CreateOrGetBitmapFont(("Data/Fonts/" + g_theDebugRenderTool->m_debugRenderConfig.m_fontName).c_str());

	if (!textToWrite->m_textFont)
	{
		delete textToWrite;
		return;
	}

	/*textToWrite->m_textFont->AddVertsForText3DAtOriginXForward(textToWrite->m_verts, size, text, startColor, 1.f, alignment);
	Mat44 mat;
	mat.SetTranslation3D(Vec3(position.x, position.y, 0.f));
	TransformVertexArray3D(textToWrite->m_verts, mat);*/
    textToWrite->m_textFont->AddVertsForText2D(textToWrite->m_verts, position, size, text, startColor);
    //textToWrite->m_textFont->AddVertsForTextInBox2D(textToWrite->m_verts, text, AABB2(position, position + Vec2(1.f, size)), size, startColor, 0.7f, alignment, OVERRUN);
	{
		std::lock_guard<std::recursive_mutex> lock(g_theDebugRenderTool->m_mutex);
    	g_theDebugRenderTool->m_screenObjects.push_back(textToWrite);
	}
	textToWrite->m_timer->Start();
}

void DebugAddMessage(const std::string& text, float duration, Camera camera, const Rgba8& startColor, const Rgba8& endColor)
{
	if (!g_theDebugRenderTool || !g_theDebugRenderTool->m_debugRenderConfig.m_renderer)
	{
		return;
	}

    DebugObject* message = new DebugObject(duration, startColor, endColor, DebugRenderMode::ALWAYS);
    message->m_isMessage = true;

    message->m_textFont = g_theDebugRenderTool->m_debugRenderConfig.m_renderer->CreateOrGetBitmapFont(("Data/Fonts/" + g_theDebugRenderTool->m_debugRenderConfig.m_fontName).c_str());

	if (!message->m_textFont)
	{
		delete message;
		return;
	}

    if (duration == 0.f)
    {
        g_theDebugRenderTool->m_messageObjects.insert(g_theDebugRenderTool->m_messageObjects.begin(), message);
    }
    else
    {
        g_theDebugRenderTool->m_messageObjects.push_back(message);
    }

    message->m_message = text;
    message->m_startColor = startColor;
    message->m_endColor = endColor;

    float windowHeight = camera.GetOrthographicTopRight().y - camera.GetOrthographicBottomLeft().y;
    float maxLines = 64.f;
    float lineHeight = windowHeight/maxLines;
    g_theDebugRenderTool->m_messageLineHeight = lineHeight;
	g_theDebugRenderTool->m_messageStartX = camera.GetOrthographicBottomLeft().x;
    g_theDebugRenderTool->m_windowHeight = camera.GetOrthographicTopRight().y;
	//float cellHeight = lineHeight * 0.7f;

	//if (duration == 0.f)
	//{
	//	message->m_textFont->AddVertsForText2D(message->m_verts, Vec2(0.f, window.y - lineHeight), cellHeight, text, startColor);
	//}
	//else
	//{
	//	message->m_textFont->AddVertsForText2D(message->m_verts, Vec2(0.f, window.y - lineHeight * (g_theDebugRenderTool->m_messageObjects.size() - 1)), cellHeight, text, startColor);
	//}

    //message->m_textFont->AddVertsForText2D(message->m_verts, Vec2(0.f, window.y - lineHeight * (g_theDebugRenderTool->m_messageObjects.size())), cellHeight, text, startColor);

	{
		std::lock_guard<std::recursive_mutex> lock(g_theDebugRenderTool->m_mutex);
    	g_theDebugRenderTool->m_screenObjects.push_back(message);
	}
	message->m_timer->Start();
}

bool Command_DebugRenderClear(EventArgs& args)
{
    UNUSED(args);
    DebugRenderClear();
    return false;
}

bool Command_DebugRenderToggle(EventArgs& args)
{
    UNUSED(args);
    if (g_theDebugRenderTool->m_isHidden)
    {
        DebugRenderSetVisible();
        return true;
    }
	if (!g_theDebugRenderTool->m_isHidden)
	{
		DebugRenderSetHidden();
        return true;
	}
    else
    {
        return false;
    }
}
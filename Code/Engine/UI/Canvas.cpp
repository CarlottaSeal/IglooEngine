#include "Engine/UI/Canvas.hpp"

Canvas::Canvas(UISystem* system, Camera* camera)
	:m_system(system), m_camera(camera)
{
	
}

Canvas::~Canvas()
{
	ShutDown();
}

void Canvas::SetCamera(Camera* camera)
{
	m_camera = camera;
}

Window* Canvas::GetSystemWindow() const
{
	return m_system->GetWindow();
}

Renderer* Canvas::GetSystemRenderer() const
{
	return m_system->GetRenderer();
}

InputSystem* Canvas::GetSystemInputSystem() const
{
	return m_system->GetInputSystem();
}

BitmapFont* Canvas::GetSystemFont() const
{
	return m_system->GetBitmapFont();
}

Camera* Canvas::GetCamera() const
{
	return m_camera;
}

void Canvas::AddElementToCanvas(UIElement* element)
{
	//for (auto & i : m_uiElementsList)
	for (auto & i : m_children)
	{
		if (!i)
		{
			i = element;
			return;
		}
	}
	m_children.push_back(element);
}

void Canvas::StartUp()
{
	if (m_parent)
	{
		SetActive(m_parent->IsActive());
	}
}

void Canvas::Update()
{
	for (auto & i : m_children)
	{
		i->Update();
	}
}

void Canvas::Render() const
{
	for (auto i : m_children)
	{
		i->Render();
	}
}

void Canvas::ShutDown()
{
	for (auto & i : m_children)
	{
		i->ShutDown();
		delete i;
		i = nullptr;
	}

	m_children.clear();
}

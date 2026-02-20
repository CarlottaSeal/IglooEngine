#pragma once
#include "Engine/UI/UISystem.h"
#include "Engine/UI/UIElement.h"

class Canvas : public UIElement
{
public:
	Canvas(UISystem* system, Camera* camera);
	~Canvas();

	void StartUp() override;
	void Update() override;
	void Render() const override;
	void ShutDown() override;

	void SetCamera(Camera* camera);

	Window* GetSystemWindow() const;
	Renderer* GetSystemRenderer() const;
	InputSystem* GetSystemInputSystem() const;
	BitmapFont* GetSystemFont() const;
	Camera* GetCamera() const;

	void AddElementToCanvas(UIElement* element);
	
	//std::vector<UIElement*> m_uiElementsList;

protected:
	Camera* m_camera = nullptr;
	UISystem* m_system = nullptr;
};
#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>

#include <string>    
#include <iostream>
#include <sstream>   
#include <vector>

#include <wrl.h>
#include <shellapi.h>

class Raytracing {

public:
	Raytracing();
	Raytracing(UINT width, UINT height, std::wstring name);
	~Raytracing();

	void Init();
	void Destroy();
	void MainLoop();

	void KeyDown(UINT8 key);
	void KeyUp(UINT8 key);

protected:

	UINT m_width;
	UINT m_height;
	float m_aspectRatio;
	std::wstring m_title;


private:
	void Update();
	void Render();

};


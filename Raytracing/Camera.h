#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>

using namespace DirectX;

class Camera {

public:
	Camera() {}
	Camera(XMVECTOR position, float ratio);
	~Camera();

	void Move(float x, float y);
	void Zoom(float delta);
	void UpdateView();

	XMMATRIX GetView() { return m_view; }
	XMMATRIX GetProjection() { return m_projection; }

private:

	float m_angle = 0.f;
	float m_yaw = 0.f;
	float m_pitch = 0.f;
	float m_roll = 0.f;

	XMVECTOR m_up;
	XMVECTOR m_front;
	XMVECTOR m_right;
			 
	XMMATRIX m_view;
	XMMATRIX m_projection;
			 
	XMVECTOR m_position;
	XMVECTOR m_target;
};


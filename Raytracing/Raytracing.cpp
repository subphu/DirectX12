#include "Raytracing.h"

Raytracing::Raytracing() {
	m_width = 900;
	m_height = 600;
	m_title = L"Raytracing";
	m_aspectRatio = static_cast<float>(m_width) / static_cast<float>(m_height);
}

Raytracing::Raytracing(UINT width, UINT height, std::wstring name) {
	m_width = width;
	m_height = height;
	m_title = name;
	m_aspectRatio = static_cast<float>(m_width) / static_cast<float>(m_height);
}

Raytracing::~Raytracing() {
}

void Raytracing::Init() {
}

void Raytracing::Destroy() {
}

void Raytracing::MainLoop() {
}

void Raytracing::KeyDown(UINT8 key) {
}

void Raytracing::KeyUp(UINT8 key) {
}

void Raytracing::Update() {
}

void Raytracing::Render() {
}

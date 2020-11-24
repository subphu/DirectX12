#include "Camera.h"


Camera::Camera(XMVECTOR position, float ratio) {
    m_position = position;
    m_target = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    m_up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    m_front = XMVector3Normalize(m_target - position);
    m_right = XMVector3Normalize(XMVector3Cross(m_front, m_up));
    m_view = XMMatrixLookAtLH(position, m_target, m_up);
    m_projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f), ratio, 1.0f, 1000.0f);

    XMFLOAT3 tempFront;
    XMStoreFloat3(&tempFront, m_front);
    m_yaw = XMConvertToDegrees(float(atan2(tempFront.z, tempFront.x)));
    m_pitch = XMConvertToDegrees(float(atan2(tempFront.y, -tempFront.z)));
}

Camera::~Camera() {}

void Camera::Move(float x, float y) {
    float distance = 0.0f;
    XMStoreFloat(&distance, XMVector3Length(m_position));

    m_position += m_right * x * 0.1f;
    m_position += m_up * y * 0.1f;

    float newDistance = 0.0f;
    XMStoreFloat(&newDistance, XMVector3Length(m_position));
    m_position = m_position * distance / newDistance;
    UpdateView();
}

void Camera::Zoom(float delta) {
    m_position += m_front * delta * 0.002f;
    m_view = XMMatrixLookAtLH(m_position, m_target, m_up);
}

void Camera::UpdateView() {
    m_front = XMVector3Normalize(m_target - m_position);
    m_right = XMVector3Normalize(XMVector3Cross(m_front, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)));
    m_up = XMVector3Normalize(XMVector3Cross(m_right, m_front));

    m_view = XMMatrixLookAtLH(m_position, m_target, m_up);
}

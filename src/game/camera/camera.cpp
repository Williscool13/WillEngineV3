//
// Created by William on 2025-11-06.
//

#include "camera.h"

namespace Game
{
void Camera::SetPlanes(float near, float far)
{
    nearPlane = near;
    farPlane = far;
}
} // Game

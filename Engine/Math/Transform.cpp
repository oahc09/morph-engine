﻿#include "Transform.hpp"
#include "Primitives/mat44.hpp"

const transform_t transform_t::IDENTITY = transform_t();

transform_t::transform_t()
  : position(0.f)
  , euler(0.f)
  , scale(1.f) {}

mat44 transform_t::localToWorld() const {
  mat44 r = mat44::makeRotation(euler);
  mat44 s = mat44::makeScale(scale.x, scale.y, scale.z);
  mat44 t = mat44::makeTranslation(position);

  return t * r * s;
}

mat44 transform_t::worldToLocal() const {
  mat44 r = mat44::makeRotation(euler).transpose();
  mat44 s = mat44::makeScale(1.f / scale.x, 1.f / scale.y, 1.f / scale.z);
  mat44 t = mat44::makeTranslation(-position);

  return s * r * t;
}

void transform_t::set(const mat44& transform) {
  position = transform.t.xyz();
  euler = transform.euler();
  scale = transform.scale();
}

mat44 Transform::localToWorld() const {
  return worldMat() * mLocalTransform.localToWorld();
}

mat44 Transform::worldToLocal() const {
  return mLocalTransform.worldToLocal() * localMat();
}

void Transform::localRotate(const Euler& euler) {
  mLocalTransform.rotate(euler);
}

void Transform::localTranslate(const vec3& offset) {
  mLocalTransform.translate(offset);
}

void Transform::setlocalTransform(const mat44& transform) {
  mLocalTransform.set(transform);
}

vec3 Transform::forward() const {
  return (localToWorld() * vec4(vec3::forward, 0)).xyz();
}

vec3 Transform::up() const {
  return (localToWorld() * vec4(vec3::up, 0)).xyz();
}

vec3 Transform::right() const {
  return (localToWorld() * vec4(vec3::right, 0)).xyz();
}

mat44 Transform::worldMat() const {
  return mParent == nullptr ? mat44::identity : mParent->localToWorld();
}

mat44 Transform::localMat() const {
  return mParent == nullptr ? mat44::identity : mParent->worldToLocal();
}
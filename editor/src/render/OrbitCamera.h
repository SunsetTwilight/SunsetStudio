#pragma once
// ============================================================
//  editor/src/render/OrbitCamera.h
//
//  ビューポートのカメラ (注視点の周りを回る)。行列は列優先 (m[列 * 4 + 行]) で、HLSL の mul(M, v) にそのまま渡せる。
//  右手座標系 (Y が上、カメラは -Z を向く)、投影後の深度は D3D と同じ 0〜1。
//  ※ SunsetMathLib の Matrix4x4::perspective は深度 -1〜1 (OpenGL 式) なので、ここでは自前で作る。
// ============================================================

#include "viewport/ViewportWindow.h"

#include <algorithm>
#include <cmath>

namespace studio
{
struct Float3
{
	float x = 0, y = 0, z = 0;
};
inline Float3 operator+(Float3 a, Float3 b)  { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Float3 operator-(Float3 a, Float3 b)  { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Float3 operator*(Float3 a, float s)   { return { a.x * s, a.y * s, a.z * s }; }
inline float  dot(Float3 a, Float3 b)        { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Float3 cross(Float3 a, Float3 b)      { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline Float3 normalize(Float3 a)
{
	const float l = std::sqrt(dot(a, a));
	return l > 0 ? a * (1.0f / l) : a;
}

// 列優先の 4x4 行列
struct Mat4
{
	float m[16] = {};

	static Mat4 identity()
	{
		Mat4 r;
		r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
		return r;
	}
	float& at(int row, int col)       { return m[col * 4 + row]; }
	float  at(int row, int col) const { return m[col * 4 + row]; }

	friend Mat4 operator*(const Mat4& a, const Mat4& b)
	{
		Mat4 r;
		for (int c = 0; c < 4; ++c)
			for (int row = 0; row < 4; ++row)
			{
				float s = 0;
				for (int k = 0; k < 4; ++k)
					s += a.at(row, k) * b.at(k, c);
				r.at(row, c) = s;
			}
		return r;
	}

	// 平行移動 * スケール
	static Mat4 translateScale(Float3 t, Float3 s)
	{
		Mat4 r = identity();
		r.at(0, 0) = s.x; r.at(1, 1) = s.y; r.at(2, 2) = s.z;
		r.at(0, 3) = t.x; r.at(1, 3) = t.y; r.at(2, 3) = t.z;
		return r;
	}

	static Mat4 lookAtRH(Float3 eye, Float3 target, Float3 up)
	{
		const Float3 z = normalize(eye - target);
		const Float3 x = normalize(cross(up, z));
		const Float3 y = cross(z, x);
		Mat4 r = identity();
		r.at(0, 0) = x.x; r.at(0, 1) = x.y; r.at(0, 2) = x.z; r.at(0, 3) = -dot(x, eye);
		r.at(1, 0) = y.x; r.at(1, 1) = y.y; r.at(1, 2) = y.z; r.at(1, 3) = -dot(y, eye);
		r.at(2, 0) = z.x; r.at(2, 1) = z.y; r.at(2, 2) = z.z; r.at(2, 3) = -dot(z, eye);
		return r;
	}

	// 逆行列 (余因子展開。正則でなければ 0 行列)
	static Mat4 inverse(const Mat4& a)
	{
		const float* m = a.m;
		Mat4 r;
		float* inv = r.m;
		inv[0]  =  m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
		inv[4]  = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
		inv[8]  =  m[4] * m[9]  * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
		inv[12] = -m[4] * m[9]  * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
		inv[1]  = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
		inv[5]  =  m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
		inv[9]  = -m[0] * m[9]  * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
		inv[13] =  m[0] * m[9]  * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
		inv[2]  =  m[1] * m[6]  * m[15] - m[1] * m[7]  * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7]  - m[13] * m[3] * m[6];
		inv[6]  = -m[0] * m[6]  * m[15] + m[0] * m[7]  * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7]  + m[12] * m[3] * m[6];
		inv[10] =  m[0] * m[5]  * m[15] - m[0] * m[7]  * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7]  - m[12] * m[3] * m[5];
		inv[14] = -m[0] * m[5]  * m[14] + m[0] * m[6]  * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6]  + m[12] * m[2] * m[5];
		inv[3]  = -m[1] * m[6]  * m[11] + m[1] * m[7]  * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9]  * m[2] * m[7]  + m[9]  * m[3] * m[6];
		inv[7]  =  m[0] * m[6]  * m[11] - m[0] * m[7]  * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8]  * m[2] * m[7]  - m[8]  * m[3] * m[6];
		inv[11] = -m[0] * m[5]  * m[11] + m[0] * m[7]  * m[9]  + m[4] * m[1] * m[11] - m[4] * m[3] * m[9]  - m[8]  * m[1] * m[7]  + m[8]  * m[3] * m[5];
		inv[15] =  m[0] * m[5]  * m[10] - m[0] * m[6]  * m[9]  - m[4] * m[1] * m[10] + m[4] * m[2] * m[9]  + m[8]  * m[1] * m[6]  - m[8]  * m[2] * m[5];
		const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
		const float s   = det != 0.0f ? 1.0f / det : 0.0f;
		for (float& x : r.m) x *= s;
		return r;
	}

	// 右手座標系、深度 0 (near) 〜 1 (far)
	static Mat4 perspectiveRH(float fovY, float aspect, float zn, float zf)
	{
		const float f = 1.0f / std::tan(fovY * 0.5f);
		Mat4 r;
		r.at(0, 0) = f / aspect;
		r.at(1, 1) = f;
		r.at(2, 2) = zf / (zn - zf);
		r.at(2, 3) = zn * zf / (zn - zf);
		r.at(3, 2) = -1.0f;
		return r;
	}
};

class OrbitCamera
{
public:
	OrbitCamera() { reset(); }

	void reset()
	{
		m_target   = { 0, 1, 0 };
		m_yaw      = 0.75f;
		m_pitch    = 0.45f;
		m_distance = 18.0f;
	}

	// viewportHeight はピクセル数 (平行移動の量を画面の大きさに合わせる)
	void apply(const CameraInput& in, float viewportHeight)
	{
		if (in.reset)
			reset();
		m_yaw   -= in.orbitX * 0.006f;
		m_pitch += in.orbitY * 0.006f;
		m_pitch  = std::clamp(m_pitch, -1.55f, 1.55f);

		if (in.panX != 0 || in.panY != 0)
		{
			const float  unitsPerPixel = 2.0f * m_distance * std::tan(m_fovY * 0.5f) / std::max(1.0f, viewportHeight);
			const Float3 fwd   = normalize(m_target - eye());
			const Float3 right = normalize(cross(fwd, { 0, 1, 0 }));
			const Float3 up    = cross(right, fwd);
			m_target = m_target - right * (in.panX * unitsPerPixel) + up * (in.panY * unitsPerPixel);
		}
		if (in.zoom != 0)
			m_distance = std::clamp(m_distance * std::pow(0.88f, in.zoom), 0.5f, 500.0f);
	}

	Float3 eye() const
	{
		const float cp = std::cos(m_pitch);
		return m_target + Float3{ cp * std::sin(m_yaw), std::sin(m_pitch), cp * std::cos(m_yaw) } * m_distance;
	}

	Mat4 view() const { return Mat4::lookAtRH(eye(), m_target, { 0, 1, 0 }); }
	Mat4 projection(float aspect) const { return Mat4::perspectiveRH(m_fovY, aspect, zNear(), zFar()); }
	float zNear() const { return 0.1f; }
	float zFar()  const { return 1000.0f; }

private:
	Float3 m_target;
	float  m_yaw = 0, m_pitch = 0, m_distance = 10;
	float  m_fovY = 0.8f;
};
} // namespace studio

// ============================================================
//  editor/src/services/Scene.cpp
// ============================================================
#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>

namespace studio
{
const SceneObject* SceneDesc::find(const std::string& id) const
{
	for (const auto& o : objects)
		if (o.id == id)
			return &o;
	return nullptr;
}

namespace
{
template<size_t N>
Json arr(const std::array<float, N>& a)
{
	Json j = Json::array();
	for (float v : a)
		j.push_back(v);
	return j;
}

bool validId(const std::string& id)
{
	if (id.empty() || id.size() > 64)
		return false;
	return std::all_of(id.begin(), id.end(), [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
	});
}

Matrix mul(const Matrix& a, const Matrix& b)
{
	Matrix r {};
	for (int c = 0; c < 4; ++c)
		for (int row = 0; row < 4; ++row)
		{
			float s = 0;
			for (int k = 0; k < 4; ++k)
				s += a[k * 4 + row] * b[c * 4 + k];
			r[c * 4 + row] = s;
		}
	return r;
}

Matrix identity()
{
	Matrix m {};
	m[0] = m[5] = m[10] = m[15] = 1;
	return m;
}
} // namespace

Json sceneToJson(const SceneDesc& s)
{
	Json objects = Json::array();
	for (const auto& o : s.objects)
	{
		Json j = {
			{ "id", o.id }, { "name", o.name }, { "model", o.model },
			{ "parent", o.parent.empty() ? Json(nullptr) : Json(o.parent) },
			{ "position", arr(o.position) }, { "rotation", arr(o.rotation) }, { "scale", arr(o.scale) },
			{ "visible", o.visible }, { "color", arr(o.color) }, { "layer", o.layer },
		};
		objects.push_back(std::move(j));
	}
	Json j = { { "version", s.version }, { "objects", objects } };
	if (!s.editor.is_null())
		j["editor"] = s.editor;
	return j;
}

bool sceneFromJson(const Json& json, SceneDesc& out, SceneDiagnostics& diag)
{
	const size_t before = diag.errors.size();
	if (!json.is_object())
		return diag.errors.push_back("(root): オブジェクトが必要です"), false;
	SceneDesc s;
	for (const auto& [key, v] : json.items())
	{
		if (key == "version")
		{
			if (!v.is_number_integer()) diag.errors.push_back("version: 整数が必要です");
			else if ((s.version = v.get<int>()) > k_SceneVersion)
				diag.warnings.push_back("version: 新しい形式のシーンです。知らない項目は無視します");
		}
		else if (key == "editor")
			s.editor = v;
		else if (key == "objects")
		{
			if (!v.is_array()) { diag.errors.push_back("objects: 配列が必要です"); continue; }
			for (size_t i = 0; i < v.size(); ++i)
			{
				const Json& jo = v[i];
				const std::string at = "objects[" + std::to_string(i) + "]";
				if (!jo.is_object()) { diag.errors.push_back(at + ": オブジェクトが必要です"); continue; }
				SceneObject o;
				auto str = [&](const Json& x, const char* k, std::string& dst) {
					if (x.is_string()) dst = x.get<std::string>();
					else diag.errors.push_back(at + "." + k + ": 文字列が必要です");
				};
				auto vec = [&](const Json& x, const char* k, auto& dst) {
					if (!x.is_array() || x.size() != dst.size()) { diag.errors.push_back(at + "." + k + ": " + std::to_string(dst.size()) + " 個の数の配列が必要です"); return; }
					for (size_t n = 0; n < dst.size(); ++n)
					{
						if (!x[n].is_number()) { diag.errors.push_back(at + "." + k + ": 数が必要です"); return; }
						dst[n] = x[n].get<float>();
						if (!std::isfinite(dst[n])) { diag.errors.push_back(at + "." + k + ": 有限の数が必要です"); return; }
					}
				};
				for (const auto& [k, x] : jo.items())
				{
					if (k == "id") str(x, "id", o.id);
					else if (k == "name") str(x, "name", o.name);
					else if (k == "model") str(x, "model", o.model);
					else if (k == "parent") { if (!x.is_null()) str(x, "parent", o.parent); }
					else if (k == "position") vec(x, "position", o.position);
					else if (k == "rotation") vec(x, "rotation", o.rotation);
					else if (k == "scale") vec(x, "scale", o.scale);
					else if (k == "color") vec(x, "color", o.color);
					else if (k == "visible") { if (x.is_boolean()) o.visible = x.get<bool>(); else diag.errors.push_back(at + ".visible: 真偽値が必要です"); }
					else if (k == "layer") str(x, "layer", o.layer);
					else diag.warnings.push_back(at + "." + k + ": 知らないキーなので無視します");
				}
				if (!validId(o.id))
					diag.errors.push_back(at + ".id: 英数字と - _ . の 1〜64 文字が必要です (" + o.id + ")");
				if (o.model != "builtin:cube")
					diag.warnings.push_back(at + ".model: 知らないモデル (" + o.model + ")。今は builtin:cube だけを描きます");
				s.objects.push_back(std::move(o));
			}
		}
		else
			diag.warnings.push_back(key + ": 知らないキーなので無視します");
	}

	// id の重複、親の存在と循環
	std::unordered_map<std::string, size_t> index;
	for (size_t i = 0; i < s.objects.size(); ++i)
		if (!index.emplace(s.objects[i].id, i).second)
			diag.errors.push_back("objects[" + std::to_string(i) + "].id: 重複しています (" + s.objects[i].id + ")");
	for (size_t i = 0; i < s.objects.size(); ++i)
	{
		const std::string& p = s.objects[i].parent;
		if (p.empty())
			continue;
		if (!index.count(p)) { diag.errors.push_back("objects[" + std::to_string(i) + "].parent: ありません (" + p + ")"); continue; }
		// 親をたどって自分に戻ったら循環
		std::string cur = p;
		for (size_t steps = 0; !cur.empty() && steps <= s.objects.size(); ++steps)
		{
			if (cur == s.objects[i].id) { diag.errors.push_back("objects[" + std::to_string(i) + "].parent: 親子が循環しています (" + s.objects[i].id + ")"); break; }
			auto it = index.find(cur);
			cur = it == index.end() ? std::string() : s.objects[it->second].parent;
		}
	}
	if (diag.errors.size() != before)
		return false;
	out = std::move(s);
	return true;
}

Matrix localMatrix(const SceneObject& o)
{
	const float k = 3.14159265358979f / 180.0f;
	const float cx = std::cos(o.rotation[0] * k), sx = std::sin(o.rotation[0] * k);
	const float cy = std::cos(o.rotation[1] * k), sy = std::sin(o.rotation[1] * k);
	const float cz = std::cos(o.rotation[2] * k), sz = std::sin(o.rotation[2] * k);
	Matrix rx = identity(), ry = identity(), rz = identity(), t = identity(), s = identity();
	rx[5] = cx;  rx[6] = sx;  rx[9] = -sx; rx[10] = cx;
	ry[0] = cy;  ry[2] = -sy; ry[8] = sy;  ry[10] = cy;
	rz[0] = cz;  rz[1] = sz;  rz[4] = -sz; rz[5] = cz;
	t[12] = o.position[0]; t[13] = o.position[1]; t[14] = o.position[2];
	s[0] = o.scale[0]; s[5] = o.scale[1]; s[10] = o.scale[2];
	return mul(t, mul(rz, mul(ry, mul(rx, s))));
}

std::vector<Matrix> worldMatrices(const SceneDesc& scene)
{
	const size_t n = scene.objects.size();
	std::unordered_map<std::string, size_t> index;
	for (size_t i = 0; i < n; ++i)
		index.emplace(scene.objects[i].id, i);
	std::vector<Matrix> out(n);
	std::vector<int>    state(n, 0);   // 0 = まだ、1 = 計算中、2 = 済み
	std::function<const Matrix&(size_t)> world = [&](size_t i) -> const Matrix& {
		if (state[i] == 2)
			return out[i];
		state[i] = 1;
		Matrix m = localMatrix(scene.objects[i]);
		const auto it = index.find(scene.objects[i].parent);
		if (it != index.end() && state[it->second] != 1)   // 循環していても止まる
			m = mul(world(it->second), m);
		out[i]   = m;
		state[i] = 2;
		return out[i];
	};
	for (size_t i = 0; i < n; ++i)
		world(i);
	return out;
}

std::vector<bool> effectiveVisibility(const SceneDesc& scene)
{
	std::unordered_map<std::string, const SceneObject*> byId;
	for (const auto& o : scene.objects)
		byId.emplace(o.id, &o);
	std::vector<bool> out;
	for (const auto& o : scene.objects)
	{
		bool v = o.visible;
		const SceneObject* cur = &o;
		for (size_t steps = 0; v && !cur->parent.empty() && steps <= scene.objects.size(); ++steps)
		{
			auto it = byId.find(cur->parent);
			if (it == byId.end()) break;
			cur = it->second;
			v   = cur->visible;
		}
		out.push_back(v);
	}
	return out;
}

SceneDesc defaultScene()
{
	SceneDesc s;
	const float palette[5][3] = {
		{ 0.80f, 0.36f, 0.20f }, { 0.25f, 0.55f, 0.80f }, { 0.35f, 0.70f, 0.40f },
		{ 0.85f, 0.75f, 0.30f }, { 0.60f, 0.45f, 0.75f },
	};
	SceneObject center;
	center.id       = "center";
	center.name     = "Center";
	center.position = { 0, 1.5f, 0 };
	center.scale    = { 3, 3, 3 };
	center.color    = { 0.92f, 0.55f, 0.30f, 1 };
	s.objects.push_back(center);
	for (int z = -3; z <= 3; ++z)
		for (int x = -3; x <= 3; ++x)
		{
			if (x == 0 && z == 0)
				continue;
			const float h = 0.6f + 1.4f * (0.5f + 0.5f * std::sin(x * 1.3f + z * 0.7f));
			const float* c = palette[(x + z + 12) % 5];
			SceneObject o;
			o.id       = "cube_" + std::to_string(x + 3) + "_" + std::to_string(z + 3);
			o.name     = "Cube (" + std::to_string(x) + ", " + std::to_string(z) + ")";
			o.position = { x * 3.0f, h * 0.5f, z * 3.0f };
			o.scale    = { 1.4f, h, 1.4f };
			o.color    = { c[0], c[1], c[2], 1 };
			s.objects.push_back(std::move(o));
		}
	return s;
}

std::string uniqueObjectId(const SceneDesc& scene, const std::string& prefix)
{
	for (int n = 1;; ++n)
	{
		std::string id = prefix + "-" + std::to_string(n);
		if (!scene.find(id))
			return id;
	}
}

int pickObject(const SceneDesc& scene, const std::vector<Matrix>& world, const std::array<float, 3>& origin, const std::array<float, 3>& dir)
{
	const std::vector<bool> visible = effectiveVisibility(scene);
	int   best  = -1;
	float bestT = std::numeric_limits<float>::max();
	for (size_t i = 0; i < scene.objects.size() && i < world.size(); ++i)
	{
		if (!visible[i])
			continue;
		const Matrix& m = world[i];
		// ワールドの光線を、箱の軸 (行列の列) に沿ったスラブで判定する (回転と拡大を含む平行六面体)
		const float c[3] = { m[12], m[13], m[14] };
		float tmin = 0, tmax = std::numeric_limits<float>::max();
		bool  hit  = true;
		for (int a = 0; a < 3 && hit; ++a)
		{
			const float ax[3] = { m[a * 4 + 0], m[a * 4 + 1], m[a * 4 + 2] };   // 箱の a 軸 (長さ = 拡大)
			const float len2  = ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2];
			if (len2 < 1e-12f) { hit = false; break; }
			// 面 n·x = n·c ± 0.5 * len2 (n = ax)
			const float nd = ax[0] * dir[0] + ax[1] * dir[1] + ax[2] * dir[2];
			const float no = ax[0] * (origin[0] - c[0]) + ax[1] * (origin[1] - c[1]) + ax[2] * (origin[2] - c[2]);
			const float h  = 0.5f * len2;
			if (std::fabs(nd) < 1e-12f)
			{
				if (no < -h || no > h) hit = false;
				continue;
			}
			float t1 = (-h - no) / nd, t2 = (h - no) / nd;
			if (t1 > t2) std::swap(t1, t2);
			tmin = std::max(tmin, t1);
			tmax = std::min(tmax, t2);
			if (tmin > tmax) hit = false;
		}
		if (hit && tmin < bestT)
		{
			bestT = tmin;
			best  = static_cast<int>(i);
		}
	}
	return best;
}
} // namespace studio

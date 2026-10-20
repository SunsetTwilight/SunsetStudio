#pragma once
// ============================================================
//  editor/src/render/LightSystem.h
//
//  ライトの種類とライトの定数バッファ (M5-5)。描画スレッドだけが使う。
//
//    種類     lights/*.light.json と *.light.hlsl を読み (0.5 秒ごとに変更を見る)、全種類をまとめた
//             shaders/generated/lights.hlsli を書く (中身が変わったときだけ。Lighting シェーダーが include し、
//             ShaderLibrary のホットリロードで作り直される)。関数のファイル (.light.hlsl) がない種類は使わない
//    定数     シーンのライト (setLights) を cbuffer Lights の形 (generateLightsHlsli と同じ) に詰めて GPU に送る
// ============================================================

#include "OrbitCamera.h"
#include "services/LightTypes.h"

#include "gfx/core/GraphicsEngine.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace studio
{
class LightSystem
{
public:
	// 描くライト (ワールド)
	struct Light
	{
		std::string type;
		Float3      position;
		Float3      direction;
		Json        params;
		uint32_t    mask = 0xFFFFFFFFu;
		bool        shadow = false;   // このライトが影を落とす (最初の 1 つだけ使う)
	};

	// root はプロジェクトのフォルダ。種類を読んで hlsli を書く
	bool init(GraphicsEngine& engine, const std::filesystem::path& root, std::string& error);
	void shutdown(GraphicsEngine& engine);

	// 種類のファイルの変更を見る (毎フレーム呼んでよい)。変わったら true
	bool poll();

	void setLights(std::vector<Light> lights);
	// 影の view × projection と値 (bias, normalBias, softness)。影を落とすライトがなければ呼ばなくてよい (setLights が消す)
	void setShadow(const Mat4& viewProj, float bias, float normalBias, float softness);
	// 必要なら定数バッファを更新して返す
	BufferHandle constants(GraphicsEngine& engine);

	const std::vector<LightTypeDef>& types() const { return m_types; }
	const LightTypeDef*              type(const std::string& id) const;
	std::vector<std::string>         takeErrors();

	static std::filesystem::path generatedPath(const std::filesystem::path& root);

private:
	void reloadTypes();
	void error(const std::string& text);

	std::filesystem::path                                    m_root;
	std::vector<LightTypeDef>                                m_types;
	std::map<std::string, std::filesystem::file_time_type>   m_stamps;   // 種類のファイル → 更新時刻
	std::vector<Light>                                       m_lights;
	Mat4                                                     m_shadowViewProj = Mat4::identity();
	float                                                    m_shadowParams[4] = {};
	BufferHandle                                             m_cb;
	bool                                                     m_dirty = true;
	std::vector<std::string>                                 m_errors;
	std::set<std::string>                                    m_reported;
	std::chrono::steady_clock::time_point                    m_lastPoll {};
};
} // namespace studio

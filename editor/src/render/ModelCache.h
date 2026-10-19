#pragma once
// ============================================================
//  editor/src/render/ModelCache.h
//
//  取り込んだモデル (SunsetAsset の Model) の GPU のバッファ (M5-3)。アセットの ID ごとに 1 つ。
//  頂点は位置と法線だけ (pipelines/gbuffer.json の入力と同じ形)。インデックスは 32 ビット。
//  ノードの階層はバインドポーズで行列にしておき、(ノード, サブメッシュ) ごとに 1 回描く。
//  色はスロットの基本色 (マテリアル (M5-4) までの仮)。スキン / モーフは使わない (バインドポーズ)。
//
//  描画スレッドだけが使う。
// ============================================================

#include "OrbitCamera.h"

#include "gfx/core/GraphicsEngine.h"
#include "sunset/asset/model/Model.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace studio
{
class ModelCache
{
public:
	struct Mesh
	{
		BufferHandle vb, ib;
	};
	struct Draw
	{
		uint32_t mesh        = 0;
		uint32_t indexOffset = 0;
		uint32_t indexCount  = 0;
		Mat4     node;            // ノードのワールド行列 (モデルの中、バインドポーズ)
		float    color[4] = { 1, 1, 1, 1 };   // スロットの基本色
	};
	struct Gpu
	{
		std::vector<Mesh> meshes;
		std::vector<Draw> draws;
		Mat4              box;    // 単位の箱 (-0.5〜0.5) を境界の箱にする行列 (選択の判定と印)
	};

	// id のモデルを (作り直して) 入れる。バッファを作れなければ false (error に理由)
	bool set(GraphicsEngine& engine, const std::string& id, const sunset::asset::Model& model, std::string& error);
	void remove(GraphicsEngine& engine, const std::string& id);
	void clear(GraphicsEngine& engine);
	const Gpu* find(const std::string& id) const;

private:
	static void destroy(GraphicsEngine& engine, Gpu& gpu);
	std::unordered_map<std::string, Gpu> m_models;
};
} // namespace studio

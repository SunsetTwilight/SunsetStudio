#pragma once
// ============================================================
//  editor/src/render/ModelCache.h
//
//  取り込んだモデル (SunsetAsset の Model) の GPU のバッファ (M5-3)。アセットの ID ごとに 1 つ。
//  頂点は MeshVertex (位置、法線、接線、UV0。マテリアルのパイプラインの入力と同じ形)。インデックスは 32 ビット。
//  ノードの階層はバインドポーズで行列にしておき、(ノード, サブメッシュ) ごとに 1 回描く。
//  マテリアルはサブメッシュのスロットで決める (M5-4)。スキン / モーフは使わない (バインドポーズ)。
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
// 頂点の形 (pipelines/materials/*.json の inputLayout と合わせる): POSITION, NORMAL, TANGENT (w = 従法線の向き), TEXCOORD0
struct MeshVertex
{
	float pos[3];
	float normal[3];
	float tangent[4];
	float uv[2];
};
static_assert(sizeof(MeshVertex) == 48);

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
		uint32_t slot        = 0; // Model::slots の番号
		Mat4     node;            // ノードのワールド行列 (モデルの中、バインドポーズ)
	};
	struct Gpu
	{
		std::vector<Mesh> meshes;
		std::vector<Draw> draws;
		std::vector<std::string> slotNames;   // スロットの名前 (シーンの materials のキー)
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

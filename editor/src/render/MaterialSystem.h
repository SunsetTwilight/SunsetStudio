#pragma once
// ============================================================
//  editor/src/render/MaterialSystem.h
//
//  マテリアルの GPU 側 (M5-4)。描画スレッドだけが使う。
//
//    マテリアル  キーで引く。ファイルのマテリアル ("materials/x.material.json") は初めて使うときに読み、変更を見て
//                読み直す。取り込んだ値で作るマテリアル ("auto:…") は setAuto で受け取る (ファイルにしない)
//    パス        マテリアルパス (drawList の layer) ごとに、パイプラインのシェーダーをリフレクションで調べ、
//                cbuffer Material の配置とテクスチャの変数 (t レジスタ) を知る。値を定数バッファに詰める
//    テクスチャ  アセットの ID ごとに GPU のテクスチャを作る (色の変数は sRGB、ほかはリニア。mip は 1 段)。
//                まだ届いていないものは既定のテクスチャで描き、takeMissingTextures で UI スレッドに頼む
//
//  バインドの約束は services/Material.h (0 = 物体、1 = フレーム、2 = マテリアル、3 = テクスチャの表)。
// ============================================================

#include "services/Material.h"

#include "gfx/core/GraphicsEngine.h"
#include "gfx/core/ShaderLibrary.h"
#include "gfx/rendergraph/PipelineLibrary.h"
#include "sunset/asset/texture/Texture.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace studio
{
// 標準のマテリアル (取り込んだ値と立方体で使う)。テンプレートの pipelines/materials/ にある
constexpr const char* k_StandardMaterialPass     = "Opaque";
constexpr const char* k_StandardMaterialPipeline = "pipelines/materials/standard.gbuffer.json";

class MaterialSystem
{
public:
	void init(GraphicsEngine& engine, ShaderLibrary& shaders, gfx::rg::PipelineLibrary& pipelines, const std::filesystem::path& root);
	void shutdown(GraphicsEngine& engine);

	// ---- マテリアル ----
	// 取り込んだ値などで作ったマテリアル (キーは "auto:" で始める)
	void setAuto(const std::string& key, const MaterialDesc& desc);
	// ファイルのマテリアルを今すぐ読み直す (エディターで保存したあと)
	void reload(const std::string& path);
	// シェーダー / パイプラインが変わったので、リフレクションを調べ直す
	void invalidateShaders();
	// ファイルの変更を見る (毎フレーム呼んでよい。0.5 秒ごとに調べる)
	void poll();

	// このマテリアルがそのマテリアルパスを持つか (読めないマテリアルは false)
	bool hasPass(const std::string& key, const std::string& pass);

	struct Bound
	{
		PipelineHandle             pipeline;
		BufferHandle               constants;   // cbuffer Material (なければ無効)
		std::vector<GfxDescriptor> textures;    // テクスチャの表 (なければ空)
		uint32_t                   numParams = 0;   // パイプラインのルートパラメーターの数 (約束の番号があるか)
	};
	// 描く前の準備。パイプラインを作れない / マテリアルを読めないなら false (理由は takeErrors)
	bool prepare(GraphicsEngine& engine, const std::string& key, const std::string& pass, const gfx::rg::PipelineTargets& targets,
	             Bound& out);

	// UI 用の説明: { key, ok, error?, material (JSON), passes: { pass: { pipeline, cbuffer: [変数], textures: [変数] } } }
	Json describe(GraphicsEngine& engine, const std::string& key);

	// ---- テクスチャ ----
	void setTexture(GraphicsEngine& engine, const std::string& id, std::shared_ptr<const sunset::asset::Texture> texture);
	std::vector<std::string> takeMissingTextures();

	std::vector<std::string> takeErrors();

private:
	struct TexBinding
	{
		std::string name;
		uint32_t    slot = 0;   // t レジスタ = テーブルの位置
	};
	struct Reflection
	{
		bool                    ok = false;
		std::string             error;
		CBufferLayout           material;   // size 0 = cbuffer Material がない
		std::vector<TexBinding> textures;
		uint32_t                tableSize = 0;
	};
	struct PassState
	{
		std::string  pipeline;
		bool         reflected = false;
		Reflection   refl;
		BufferHandle cb;
		uint32_t     cbSize = 0;
		bool         dirty = true;
	};
	struct Instance
	{
		bool                             file = false;
		bool                             loaded = false;
		bool                             ok = false;
		std::string                      error;
		MaterialDesc                     desc;
		std::filesystem::file_time_type  time {};
		std::map<std::string, PassState> passes;
	};
	struct GpuTexture
	{
		std::shared_ptr<const sunset::asset::Texture> cpu;
		TextureHandle srgb, linear;
	};

	Instance& instance(const std::string& key);
	void      load(Instance& inst, const std::string& path);
	void      reflect(PassState& ps);
	TextureHandle texture(GraphicsEngine& engine, const std::string& id, bool srgb);
	TextureHandle makeTexture(GraphicsEngine& engine, const sunset::asset::Texture& t, bool srgb, const std::string& name);
	void          error(const std::string& text);
	void          releasePasses(GraphicsEngine& engine, Instance& inst);

	GraphicsEngine*           m_engine    = nullptr;
	ShaderLibrary*            m_shaders   = nullptr;
	gfx::rg::PipelineLibrary* m_pipelines = nullptr;
	std::filesystem::path     m_root;
	std::map<std::string, Instance>   m_instances;
	std::map<std::string, GpuTexture> m_textures;
	std::set<std::string>             m_missing;     // 頼んだがまだ届かないテクスチャ
	std::set<std::string>             m_requested;   // 一度頼んだもの (何度も頼まない)
	TextureHandle                     m_white, m_flatNormal;
	std::vector<std::string>          m_errors;
	std::set<std::string>             m_reported;    // 同じエラーを何度も出さない
	std::chrono::steady_clock::time_point m_lastPoll {};
};
} // namespace studio

#pragma once
// ============================================================
//  editor/src/render/NodeResources.h
//
//  描画ノードのリソースを、物体ごとにつなぐ (2026-10-22)。描画スレッドだけが使う。
//
//    インターフェース  描画ノード (drawList のパス) のパイプラインのルートパラメーターのうち、グラフ (パスの constants /
//                      入出力の bind) がつながないものを、シェーダーのリフレクションと合わせて調べる:
//                        CBV             → 物体ごとの定数バッファ (その b レジスタの cbuffer の変数)
//                        SRV のテーブル  → 物体ごとのテクスチャの表 (Texture2D の t レジスタ → 表の位置)
//                        ほかの種類      → まだつなげない (描かずに理由を出す)
//    つなぐ            描画ごとに、決まった値 (services/NodeParams の mergeNodeParams) を定数バッファに詰め、
//                      テクスチャの表を作る。値が変わらなければ作り直さない (Cache)
//    テクスチャ        アセットの ID ごとに GPU のテクスチャを作る (色の変数は sRGB、ほかはリニア。mip は 1 段)。
//                      まだ届いていないものは既定のテクスチャで描き、takeMissingTextures で UI スレッドに頼む
// ============================================================

#include "services/NodeParams.h"

#include "gfx/core/GraphicsEngine.h"
#include "gfx/core/ShaderLibrary.h"
#include "gfx/rendergraph/Compiler.h"
#include "gfx/rendergraph/PipelineLibrary.h"
#include "sunset/asset/texture/Texture.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace studio
{
class NodeResources
{
public:
	void init(GraphicsEngine& engine, ShaderLibrary& shaders, gfx::rg::PipelineLibrary& pipelines);
	void shutdown(GraphicsEngine& engine);
	// シェーダー / パイプラインが変わったので、インターフェースを調べ直す
	void invalidate() { m_interfaces.clear(); ++m_generation; }

	struct Texture
	{
		std::string name;
		uint32_t    slot = 0;   // 表の位置
	};
	struct Interface
	{
		bool        ok = false;
		std::string error;     // 調べられない / つなげないルートパラメーターがある (描かない)
		struct CBuffer { uint32_t param = 0; CBufferLayout layout; };
		struct Table   { uint32_t param = 0; uint32_t size = 0; std::vector<Texture> textures; };
		std::vector<CBuffer> cbuffers;
		std::vector<Table>   tables;
		bool empty() const { return cbuffers.empty() && tables.empty(); }
	};
	// パスのインターフェース (パイプラインとパスのバインドで覚えておく)
	const Interface& interfaceOf(const gfx::rg::PassDesc& pass);

	// 描画ごとの GPU のもの
	struct Cache
	{
		uint64_t                   generation = 0;   // インターフェースを調べ直したら作り直す
		uint64_t                   version    = 0;   // 値が変わったら作り直す
		std::vector<BufferHandle>  buffers;          // cbuffers と同じ順
		std::vector<std::vector<GfxDescriptor>> tables;
		bool                       missing = false;  // 届いていないテクスチャがある (届いたら作り直す)
	};
	// 作り直しが要るか (値の版が変わった、テクスチャが届いた、インターフェースを調べ直した)
	bool stale(const Interface& itf, const Cache& cache, uint64_t version) const
	{
		return cache.generation != m_generation || cache.version != version * 1000003ull + m_textureVersion ||
		       cache.buffers.size() != itf.cbuffers.size() || cache.tables.size() != itf.tables.size();
	}
	// 値 (決まったもの) をつなぐ準備。version は値の版 (同じなら作り直さない)
	void prepare(GraphicsEngine& engine, const Interface& itf, const Json& values, uint64_t version, Cache& cache);
	static void release(GraphicsEngine& engine, Cache& cache);
	// 描画のコマンドに、物体ごとのリソースをつなぐ
	static void bind(ICommandBuffer* cb, const Interface& itf, const Cache& cache);

	// UI 用: { ok, error?, cbuffers: [{ name, vars: [{ name, type, columns, elements }] }],
	//          textures: [{ name, srgb, default: "white" | "flatNormal" }] }
	static Json describe(const Interface& itf);

	// ---- テクスチャ ----
	void setTexture(GraphicsEngine& engine, const std::string& id, std::shared_ptr<const sunset::asset::Texture> texture);
	std::vector<std::string> takeMissingTextures();

	std::vector<std::string> takeErrors();

private:
	struct GpuTexture
	{
		std::shared_ptr<const sunset::asset::Texture> cpu;
		TextureHandle srgb, linear;
	};
	Interface     build(const gfx::rg::PassDesc& pass);
	TextureHandle texture(GraphicsEngine& engine, const std::string& id, bool srgb, bool& missing);
	TextureHandle makeTexture(GraphicsEngine& engine, const sunset::asset::Texture& t, bool srgb, const std::string& name);
	void          error(const std::string& text);

	GraphicsEngine*           m_engine    = nullptr;
	ShaderLibrary*            m_shaders   = nullptr;
	gfx::rg::PipelineLibrary* m_pipelines = nullptr;
	std::map<std::string, Interface>  m_interfaces;   // パイプライン + つないでいる番号
	uint64_t                          m_generation = 1;
	std::map<std::string, GpuTexture> m_textures;
	uint64_t                          m_textureVersion = 0;   // テクスチャが届くたびに上げる
	std::set<std::string>             m_missing;
	std::set<std::string>             m_requested;
	TextureHandle                     m_white, m_flatNormal;
	std::vector<std::string>          m_errors;
	std::set<std::string>             m_reported;
};
} // namespace studio

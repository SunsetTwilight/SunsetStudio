// ============================================================
//  editor/src/render/ModelCache.cpp
// ============================================================
#include "ModelCache.h"

#include <algorithm>

namespace studio
{
namespace
{
// ViewportRenderer.cpp の Vertex と同じ形 (pipelines/gbuffer.json の入力)
struct Vertex
{
	float pos[3];
	float normal[3];
};
} // namespace

bool ModelCache::set(GraphicsEngine& engine, const std::string& id, const sunset::asset::Model& model, std::string& error)
{
	remove(engine, id);
	Gpu gpu;
	for (const sunset::asset::ModelMesh& m : model.meshes)
	{
		Mesh mesh;
		if (m.vertexCount > 0 && !m.indices.empty())
		{
			std::vector<Vertex> vertices(m.vertexCount);
			const bool hasNormals = m.normals.size() >= size_t(m.vertexCount) * 3;
			for (uint32_t v = 0; v < m.vertexCount; ++v)
			{
				Vertex& out = vertices[v];
				std::copy_n(&m.positions[size_t(v) * 3], 3, out.pos);
				if (hasNormals)
					std::copy_n(&m.normals[size_t(v) * 3], 3, out.normal);
				else
					out.normal[0] = 0, out.normal[1] = 1, out.normal[2] = 0;
			}
			mesh.vb = engine.createBuffer({ vertices.size() * sizeof(Vertex), BufferUsage::Vertex, vertices.data(), sizeof(Vertex) });
			mesh.ib = engine.createBuffer({ m.indices.size() * sizeof(uint32_t), BufferUsage::Index, m.indices.data(), sizeof(uint32_t) });
			if (!mesh.vb.valid() || !mesh.ib.valid())
			{
				gpu.meshes.push_back(mesh);
				destroy(engine, gpu);
				error = "メッシュのバッファを作れません: " + m.name;
				return false;
			}
		}
		gpu.meshes.push_back(mesh);
	}

	// メッシュを持つノードごとに、サブメッシュを描く
	const std::vector<std::array<float, 16>> world = sunset::asset::nodeWorldMatrices(model);
	for (size_t n = 0; n < model.nodes.size(); ++n)
	{
		const int32_t mi = model.nodes[n].mesh;
		if (mi < 0 || static_cast<size_t>(mi) >= model.meshes.size() || !gpu.meshes[static_cast<size_t>(mi)].vb.valid())
			continue;
		const sunset::asset::ModelMesh& m = model.meshes[static_cast<size_t>(mi)];
		for (const sunset::asset::SubMesh& sm : m.submeshes)
		{
			if (sm.indexCount == 0)
				continue;
			Draw d;
			d.mesh        = static_cast<uint32_t>(mi);
			d.indexOffset = sm.indexOffset;
			d.indexCount  = sm.indexCount;
			std::copy(world[n].begin(), world[n].end(), d.node.m);
			if (sm.slot < model.slots.size())
				std::copy_n(model.slots[sm.slot].baseColor.begin(), 4, d.color);
			gpu.draws.push_back(d);
		}
	}

	// 境界の箱 (潰れた軸は少しだけ厚くして、選択の判定が効くようにする)
	Float3 lo{ model.boundsMin[0], model.boundsMin[1], model.boundsMin[2] };
	Float3 hi{ model.boundsMax[0], model.boundsMax[1], model.boundsMax[2] };
	const Float3 center{ (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f };
	const Float3 size{ std::max(hi.x - lo.x, 1e-3f), std::max(hi.y - lo.y, 1e-3f), std::max(hi.z - lo.z, 1e-3f) };
	gpu.box = Mat4::translateScale(center, size);

	m_models.emplace(id, std::move(gpu));
	return true;
}

void ModelCache::remove(GraphicsEngine& engine, const std::string& id)
{
	auto it = m_models.find(id);
	if (it == m_models.end())
		return;
	destroy(engine, it->second);
	m_models.erase(it);
}

void ModelCache::clear(GraphicsEngine& engine)
{
	for (auto& [id, gpu] : m_models)
		destroy(engine, gpu);
	m_models.clear();
}

const ModelCache::Gpu* ModelCache::find(const std::string& id) const
{
	auto it = m_models.find(id);
	return it != m_models.end() ? &it->second : nullptr;
}

void ModelCache::destroy(GraphicsEngine& engine, Gpu& gpu)
{
	// エンジンが GPU の使い終わりまで解放を遅らせる
	for (Mesh& m : gpu.meshes)
	{
		if (m.vb.valid()) engine.destroyBuffer(m.vb);
		if (m.ib.valid()) engine.destroyBuffer(m.ib);
	}
	gpu.meshes.clear();
	gpu.draws.clear();
}
} // namespace studio

#pragma once
// ============================================================
//  editor/src/services/Scene.h
//
//  シーンのファイル (scenes/*.scene.json、M5-2): 何を描くか。どう描くかはレンダーグラフ。
//  GPU も webview も使わないので単体テストできる。
//
//    { "version": 1,
//      "objects": [ { "id": "cube-1", "name": "Center", "model": "builtin:cube", "parent": null,
//                     "position": [0, 1.5, 0], "rotation": [0, 0, 0], "scale": [3, 3, 3],
//                     "visible": true, "color": [0.92, 0.55, 0.30, 1], "layer": "Opaque" } ],
//      "editor": { … } }   // エディターの情報 (読み書きで保つだけ)
//
//  ・id はシーンの中で一意 (英数字と - _ .)。parent は親の id (null = 一番上)。循環はエラー
//  ・rotation はオイラー角 (度)。X → Y → Z の順に回す (world = 親 × T × Rz × Ry × Rx × S)
//  ・model は "builtin:cube" (立方体) か "asset:<ID>" (取り込んだモデル。ID は assets/ の .meta の id、M5-3)
//  ・color と layer はマテリアル (M5-4) ができるまでの仮の項目。layer はレンダーグラフの drawList の layer と合わせる
//  行列は列優先 (m[列 * 4 + 行]、OrbitCamera.h の Mat4 と同じ)。
// ============================================================

#include <nlohmann/json.hpp>

#include <array>
#include <string>
#include <vector>

namespace studio
{
using Json = nlohmann::json;

constexpr int k_SceneVersion = 1;

struct SceneObject
{
	std::string          id;
	std::string          name;
	std::string          model    = "builtin:cube";
	std::string          parent;                       // 空 = 一番上
	std::array<float, 3> position { 0, 0, 0 };
	std::array<float, 3> rotation { 0, 0, 0 };        // 度 (X → Y → Z の順に回す)
	std::array<float, 3> scale    { 1, 1, 1 };
	bool                 visible  = true;
	std::array<float, 4> color    { 0.8f, 0.8f, 0.8f, 1 };   // 仮 (M5-4 でマテリアルに)
	std::string          layer    = "Opaque";                // 仮 (M5-4 でマテリアルパスに)
};

struct SceneDesc
{
	int                      version = k_SceneVersion;
	std::vector<SceneObject> objects;
	Json                     editor;   // 読み書きで保つだけ (null なら書かない)

	const SceneObject* find(const std::string& id) const;
};

struct SceneDiagnostics
{
	std::vector<std::string> errors;     // "objects[3].parent: …" の形
	std::vector<std::string> warnings;
};

Json sceneToJson(const SceneDesc& scene);
// 型の違い、id の重複 / 不正、親がない、親の循環はエラー。知らないキーは警告
bool sceneFromJson(const Json& json, SceneDesc& out, SceneDiagnostics& diag);

using Matrix = std::array<float, 16>;   // 列優先
// ローカルの変換 (T × Rz × Ry × Rx × S)
Matrix localMatrix(const SceneObject& o);
// objects と同じ順に、ワールドの行列を返す (親の変換を掛ける)。sceneFromJson を通ったシーンを渡すこと
std::vector<Matrix> worldMatrices(const SceneDesc& scene);
// 親が見えなければ子も見えない
std::vector<bool> effectiveVisibility(const SceneDesc& scene);

// 新しいプロジェクトの既定のシーン (M4 までの固定の立方体と同じ並び: 中央の大きな立方体 + 周りの 48 個)
SceneDesc defaultScene();

// 使われていない id を作る ("cube-12" など)
std::string uniqueObjectId(const SceneDesc& scene, const std::string& prefix);

// ---- ビューポートでのクリックの選択 (CPU の光線と箱の判定) ----
// 光線 (origin + t * dir) と、各物体の単位の箱 (-0.5〜0.5 をワールドの行列で変換したもの) の最初の交差。
// 見えている物体だけ。当たらなければ -1
int pickObject(const SceneDesc& scene, const std::vector<Matrix>& world, const std::array<float, 3>& origin,
               const std::array<float, 3>& dir);
} // namespace studio

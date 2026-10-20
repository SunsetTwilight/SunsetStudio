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
//                     "visible": true, "color": [0.92, 0.55, 0.30, 1],
//                     "materials": { "Body": "materials/skin.material.json" } } ],
//      "editor": { … } }   // エディターの情報 (読み書きで保つだけ)
//
//  ・id はシーンの中で一意 (英数字と - _ .)。parent は親の id (null = 一番上)。循環はエラー
//  ・rotation はオイラー角 (度)。X → Y → Z の順に回す (world = 親 × T × Rz × Ry × Rx × S)
//  ・model は "builtin:cube" (立方体) か "asset:<ID>" (取り込んだモデル。ID は assets/ の .meta の id、M5-3)
//  ・materials はスロットの名前 → マテリアルのファイル (M5-4)。書いていないスロットは、取り込んだ値 (モデルのスロットの
//    基本色とテクスチャ) の標準のマテリアルで描く。立方体のスロットは "Default"
//  ・color はマテリアルの色に掛ける色 (同じモデルを 1 体ずつ色違いにする)。M5-2 の layer は読み飛ばす
//  ・light があれば、その物体はライト (M5-5。モデルは描かない)。
//      "light": { "type": "point", "params": { "color": [1, 1, 1], "intensity": 5 }, "links": ["hero"] }
//    type は lights/<種類>.light.json の種類、params はその値 (書いていないものは既定値)、
//    links は照らす物体の id (子も含む。空 = すべての物体。ライトリンク)。向きはローカルの -Z
//    "shadow": { "enabled": true, "bias": 0.001, "normalBias": 0.03, "softness": 1.5 } は影 (今は最初の平行光源だけ)
//  ・castShadow: false の物体は影を落とさない (書いていなければ落とす)
//  ・layers は描画レイヤー (グラフの drawList の layer) の名前の配列。この物体を描くノードを選ぶ。
//    slotLayers はスロットの名前 → 描画レイヤーで、そのスロットだけ上書きする。どちらも書いていなければ既定の
//    "Opaque"。空の配列は「どこにも描かない」(エディターがピンクで見せる)。Shadow は castShadow で決まる
//  行列は列優先 (m[列 * 4 + 行]、OrbitCamera.h の Mat4 と同じ)。
// ============================================================

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace studio
{
using Json = nlohmann::json;

constexpr int k_SceneVersion = 1;

struct SceneShadow
{
	bool  enabled    = false;
	float bias       = 0.001f;   // 深さの比較のずれ (影のにじみ / 浮きの調整)
	float normalBias = 0.03f;    // 法線の向きに押し出す量 (m。斜めの面の縞を消す)
	float softness   = 1.5f;     // 縁のぼかし (テクセル)
	bool operator==(const SceneShadow&) const = default;
};

struct SceneLight
{
	std::string              type = "point";
	Json                     params = Json::object();
	std::vector<std::string> links;   // 照らす物体の id (空 = すべて)
	SceneShadow              shadow;
};

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
	std::array<float, 4> color    { 1, 1, 1, 1 };   // マテリアルの色に掛ける
	std::map<std::string, std::string> materials;   // スロット → マテリアルのファイル (書いていなければ取り込んだ値)
	std::optional<SceneLight> light;                 // あればライト (M5-5)
	bool                      castShadow = true;     // 影を落とす
	std::optional<std::vector<std::string>> layers;  // 描画レイヤー (なければ既定)
	std::map<std::string, std::vector<std::string>> slotLayers;   // スロット → 描画レイヤー (物体の指定を上書き)
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

// ---- ライトリンク (M5-5) ----
// links のあるライトに 1 ビットずつ割り当て (シーンの順、32 個まで)、物体のマスクに、その物体 (や親) を照らす
// ライトのビットを立てる。links のないライトのマスクは 0xFFFFFFFF (すべての物体を照らす)。
// G-Buffer に物体のマスクを書き、Lighting で (ライトのマスク == 全部) か (ライトのマスク & 物体のマスク) != 0 なら照らす
constexpr uint32_t k_LightMaskAll = 0xFFFFFFFFu;
struct LightLinks
{
	std::vector<uint32_t>    objectMasks;   // objects と同じ順 (ライトの物体は 0)
	std::vector<uint32_t>    lightMasks;    // objects と同じ順 (ライトでない物体は 0)
	std::vector<std::string> warnings;      // 32 個を超えたなど
};
LightLinks computeLightLinks(const SceneDesc& scene);
// ワールドの行列から、ライトの向き (ローカルの -Z、正規化)
std::array<float, 3> lightDirection(const Matrix& world);

// 平行光源の影の view × projection (正射影、D3D の深度 0〜1)。boxes (単位の箱をワールドへ写す行列) の全体が
// 写るように合わせる (カメラに依存しないので、カメラを動かしても影が揺れない)。boxes が空なら単位行列
Matrix directionalShadowMatrix(const std::array<float, 3>& direction, const std::vector<Matrix>& boxes);
// 列優先の行列で点を写す (w で割る)
std::array<float, 3> transformPoint(const Matrix& m, const std::array<float, 3>& p);

// ---- 描画レイヤー ----
constexpr const char* k_DefaultDrawLayer = "Opaque";   // 何も指定していない物体のレイヤー
// 物体のスロットを描くレイヤー: スロットの上書き → 物体の layers → 既定 ("Opaque")
std::vector<std::string> drawLayersOf(const SceneObject& o, const std::string& slot);

// 新しいプロジェクトの既定のシーン (M4 までの固定の立方体と同じ並び: 中央の大きな立方体 + 周りの 48 個、
// 影を受ける地面 "Ground" (薄い箱)、今までの光と同じ向きの、影を落とす平行光源 "Sun" (最後))
SceneDesc defaultScene();

// 使われていない id を作る ("cube-12" など)
std::string uniqueObjectId(const SceneDesc& scene, const std::string& prefix);

// ---- ビューポートでのクリックの選択 (CPU の光線と箱の判定) ----
// 光線 (origin + t * dir) と、各物体の単位の箱 (-0.5〜0.5 をワールドの行列で変換したもの) の最初の交差。
// 見えている物体だけ。当たらなければ -1
int pickObject(const SceneDesc& scene, const std::vector<Matrix>& world, const std::array<float, 3>& origin,
               const std::array<float, 3>& dir);
} // namespace studio

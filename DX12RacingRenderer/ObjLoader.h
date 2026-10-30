#pragma once
#include <filesystem>
#include <vector>
#include <cstdint>

#include "Renderer.h"

/// @brief OBJ ファイルを読み込む
/// @param filePath .exe から OBJファイルへの相対パス
/// @param vertices 頂点データ
/// @param indices インデックスデータ
/// @return 成功したら true
bool LoadObj(const std::filesystem::path& filePath, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

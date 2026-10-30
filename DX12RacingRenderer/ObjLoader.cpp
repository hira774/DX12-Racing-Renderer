#include "ObjLoader.h"

#include <DirectXMath.h>

#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>

#include "Logger.h"
#include "StringUtils.h"

bool LoadObj(const std::filesystem::path& filePath, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
	wchar_t exePath[MAX_PATH]{};
	// exeのパスを取得
	DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	// 0 が返ってきたなら失敗
	// MAX_PATH と同じなら途中で切れている
	if (length == 0 || length == MAX_PATH)
	{
		Logger::Error("Failed to get module file name.");
		return false;
	}
	// .exe と同じフォルダの
	// objファイルのパスを作成
	std::filesystem::path objPath = std::filesystem::path(exePath).parent_path() / filePath;

	// ファイルを開く
	std::ifstream file(objPath);
	// 開けたか確認
	if (!file)
	{
		Logger::Error("Failed to open OBJ file. " + StringUtils::Utf16ToUtf8(objPath.wstring()));
		return false;
	}

	// 頂点情報の一覧
	std::vector<DirectX::XMFLOAT3> positions;
	std::vector<DirectX::XMFLOAT2> uvs;
	std::vector<DirectX::XMFLOAT3> normals;

	// 1行ずつ読む
	// 1行分の文字を入れる
	std::string line;
	// ファイルの最後まで回す
	while (std::getline(file, line))
	{
		std::istringstream ss(line);
		std::string prefix;
		ss >> prefix;
		// 位置
		if (prefix == "v")
		{
			DirectX::XMFLOAT3 pos = {};
			ss >> pos.x >> pos.y >> pos.z;	// 残りの3つを読む
			pos.z = -pos.z;					// 右手系 を 左手系 の座標に変換
			positions.push_back(pos);		// 一覧の最後に足す
		}
		// UV
		else if (prefix == "vt")
		{
			DirectX::XMFLOAT2 uv = {};
			ss >> uv.x >> uv.y;
			uv.y = 1.0f - uv.y;	// 上下を逆にする
			uvs.push_back(uv);
		}
		// 法線
		else if (prefix == "vn")
		{
			DirectX::XMFLOAT3 normal = {};
			ss >> normal.x >> normal.y >> normal.z;
			normal.z = -normal.z;
			normals.push_back(normal);
		}
		// 面
		else if (prefix == "f")
		{
			std::vector<std::string> corners;
			// 角一つ分の文字列 (OBJは 1/2/3 の形)
			std::string corner;
			while (ss >> corner)
			{
				// "/" を " " に置き換える
				std::ranges::replace(corner, '/', ' ');
				corners.push_back(corner);
			}
			// 3角形のみ対応。
			// 4角形、n角形ポリゴンは非対応
			if (corners.size() != 3)
			{
				Logger::Error("OBJ face must be a triangle.");
				return false;
			}

			// 頂点を書き始める場所
			uint32_t start = static_cast<uint32_t>(vertices.size());

			// 角一つ分の文字列を 3つの値(int) に変換する
			for (const std::string& c : corners)
			{
				std::istringstream cornerSs(c);
				int positionIndex = 0;	// 位置の番号
				int uvIndex = 0;		// UVの番号
				int normalIndex = 0;	// 法線の番号
				cornerSs >> positionIndex >> uvIndex >> normalIndex;

				// OBJ は 1スタートなので -1 して配列と同じ形にする
				--positionIndex;
				--uvIndex;
				--normalIndex;
				// 配列の範囲内か確認
				bool checkPos = positionIndex < 0 || positionIndex >= static_cast<int>(positions.size());
				bool checkUV = uvIndex < 0 || uvIndex >= static_cast<int>(uvs.size());
				bool checkNor = normalIndex < 0 || normalIndex >= static_cast<int>(normals.size());
				if (checkPos || checkUV || checkNor)
				{
					Logger::Error("OBJ face index is out of range.");
					return false;
				}

				// Vertex構造体にする
				Vertex vertex = {};
				// 位置
				vertex.position[0] = positions[positionIndex].x;
				vertex.position[1] = positions[positionIndex].y;
				vertex.position[2] = positions[positionIndex].z;
				// 法線
				vertex.normal[0] = normals[normalIndex].x;
				vertex.normal[1] = normals[normalIndex].y;
				vertex.normal[2] = normals[normalIndex].z;
				// UV
				vertex.uv[0] = uvs[uvIndex].x;
				vertex.uv[1] = uvs[uvIndex].y;

				vertices.push_back(vertex);
			}
			// 時計回りにインデックスを書き込む
			indices.push_back(start + 0);
			indices.push_back(start + 2);
			indices.push_back(start + 1);
		}
	}

	// 面の読み込みに失敗
	if (vertices.empty())
	{
		Logger::Error("OBJ file has no faces.");
		return false;
	}

	return true;
}

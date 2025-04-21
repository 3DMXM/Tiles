/**
 *  合并 瓦块集
 *  瓦片格式: \tiles\z_y_x.jpg
 *  如 \tiles\15_16278_16281.jpg
 */

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <regex>
#include <opencv2/opencv.hpp>

namespace fs = std::filesystem;

// 瓦片信息结构
struct TileInfo
{
    int z;            // 缩放级别
    int x;            // x坐标
    int y;            // y坐标
    std::string path; // 文件路径
};

// 从文件名解析瓦片信息
TileInfo parseTileFilename(const std::string &filepath)
{
    std::regex pattern("(\\d+)_(\\d+)_(\\d+)\\.jpg");
    std::smatch matches;
    std::string filename = fs::path(filepath).filename().string();

    TileInfo info;
    info.path = filepath;

    if (std::regex_search(filename, matches, pattern))
    {
        info.z = std::stoi(matches[1]);
        info.y = std::stoi(matches[2]);
        info.x = std::stoi(matches[3]);
    }

    return info;
}

// 扫描目录获取所有瓦片信息
std::vector<TileInfo> scanTiles(const std::string &directory)
{
    std::vector<TileInfo> tiles;

    for (const auto &entry : fs::recursive_directory_iterator(directory))
    {
        // if (entry.is_regular_file() && entry.path().extension() == ".jpg")
        // {
        tiles.push_back(parseTileFilename(entry.path().string()));
        // }
    }

    return tiles;
}

// 计算合并后图像的尺寸和瓦片位置
void calculateDimensions(const std::vector<TileInfo> &tiles, int &minX, int &minY, int &maxX, int &maxY)
{
    if (tiles.empty())
        return;

    minX = maxX = tiles[0].x;
    minY = maxY = tiles[0].y;

    for (const auto &tile : tiles)
    {
        minX = std::min(minX, tile.x);
        minY = std::min(minY, tile.y);
        maxX = std::max(maxX, tile.x);
        maxY = std::max(maxY, tile.y);
    }
}

// 合并瓦片
cv::Mat mergeTiles(const std::vector<TileInfo> &tiles, int tileSize = 256)
{
    if (tiles.empty())
    {
        printf("No tiles found\n");
        return cv::Mat();
    }

    // 确保所有瓦片都在同一个缩放级别
    int zoomLevel = tiles[0].z;
    for (const auto &tile : tiles)
    {
        if (tile.z != zoomLevel)
        {
            printf("Warning: Tiles have inconsistent zoom levels, merge results may be abnormal\n");
            break;
        }
    }

    // 计算瓦片范围
    int minX, minY, maxX, maxY;
    calculateDimensions(tiles, minX, minY, maxX, maxY);

    // 输出 minX, minY, maxX, maxY
    printf("minX: %d, minY: %d, maxX: %d, maxY: %d\n", minX, minY, maxX, maxY);

    // 计算合并后图像的尺寸
    int width = (maxX - minX + 1) * tileSize;
    int height = (maxY - minY + 1) * tileSize;

    printf("Merged image size: %dx%d pixels\n", width, height);
    printf("Tile range: X(%d-%d), Y(%d-%d)\n", minX, maxX, minY, maxY);

    // 创建合并后的图像
    cv::Mat result(height, width, CV_8UC3, cv::Scalar(255, 255, 255));

    // 合并瓦片
    for (const auto &tile : tiles)
    {
        cv::Mat tileImg = cv::imread(tile.path);

        if (tileImg.empty())
        {
            printf("Cannot read tile: %s\n", tile.path.c_str());
            continue;
        }

        // 计算瓦片在合并图像中的位置
        int x = (tile.x - minX) * tileSize;
        int y = (tile.y - minY) * tileSize;

        printf("tileImg: %s, x: %d, y: %d\n", tile.path.c_str(), x, y);

        // 创建目标区域
        cv::Rect roi(x, y, tileSize, tileSize);

        // 确保ROI在图像范围内
        if (roi.x >= 0 && roi.y >= 0 && roi.x + roi.width <= result.cols && roi.y + roi.height <= result.rows)
        {
            // 将瓦片复制到结果图像
            tileImg.copyTo(result(roi));
        }
    }

    return result;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: %s <tile directory> [output filename]\n", argv[0]);
        return 1;
    }

    std::string tileDirectory = argv[1];
    std::string outputFile = (argc > 2) ? argv[2] : "merged_tiles.jpg";

    printf("Scanning tile directory: %s\n", tileDirectory.c_str());

    // 扫描目录获取瓦片信息
    std::vector<TileInfo> tiles = scanTiles(tileDirectory);

    printf("Found %zu tile files\n", tiles.size());

    if (tiles.empty())
    {
        printf("No tile files found, please check directory path\n");
        return 1;
    }

    // 合并瓦片
    cv::Mat mergedImage = mergeTiles(tiles);

    if (mergedImage.empty())
    {
        printf("Failed to merge tiles\n");
        return 1;
    }

    // 保存合并后的图像
    printf("Saving merged image to: %s\n", outputFile.c_str());
    cv::imwrite(outputFile, mergedImage);

    printf("Tile merge complete!\n");

    // 按任意键继续
    printf("Press any key to exit...\n");
    std::cin.get();

    return 0;
}

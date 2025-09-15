/**
 *  合并 瓦块集
 *  瓦片格式: \tiles\z_y_x.jpg
 *  如 \tiles\15_16278_16281.jpg
 *
 *  使用方式:
 *  1. 直接运行（使用当前目录下的config.ini）:
 *     mergeTiles.exe
 *
 *  2. 指定配置文件:
 *     mergeTiles.exe path/to/config.ini
 *
 *  配置文件格式 (config.ini):
 *  [merge]
 *  inputDir=tiles                 ; 瓦片输入目录
 *  outputFile=merged_tiles.jpg    ; 输出文件名
 *  tileSize=256                   ; 瓦片大小
 *  order=top-to-bottom            ; 合并顺序: top-to-bottom, left-to-right, bottom-to-top, right-to-left
 *  rotation=0                     ; 旋转角度: 0, 90, 180, 270
 *  flip=none                      ; 翻转类型: none, horizontal, vertical, both
 */

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <regex>
#include <opencv2/opencv.hpp>
#include "INIReader.h"

namespace fs = std::filesystem;

// 瓦片信息结构
struct TileInfo
{
    int z;            // 缩放级别
    int x;            // x坐标
    int y;            // y坐标
    std::string path; // 文件路径
};

// 合并顺序枚举
enum class MergeOrder
{
    TOP_TO_BOTTOM, // 从上到下
    LEFT_TO_RIGHT, // 从左到右
    BOTTOM_TO_TOP, // 从下到上
    RIGHT_TO_LEFT  // 从右到左
};

// 旋转角度枚举
enum class RotationAngle
{
    NONE = 0,   // 不旋转
    CW_90 = 1,  // 顺时针90度
    CW_180 = 2, // 顺时针180度
    CW_270 = 3  // 顺时针270度
};

// 翻转类型枚举
enum class FlipType
{
    NONE = 0,       // 不翻转
    HORIZONTAL = 1, // 水平翻转
    VERTICAL = 2,   // 垂直翻转
    BOTH = 3        // 水平和垂直翻转
};

// 变换配置结构
struct TransformConfig
{
    MergeOrder order;
    RotationAngle rotation;
    FlipType flip;

    TransformConfig() : order(MergeOrder::TOP_TO_BOTTOM), rotation(RotationAngle::NONE), flip(FlipType::NONE) {}
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

// 应用图像变换（旋转和翻转）
cv::Mat applyTransform(const cv::Mat &image, RotationAngle rotation, FlipType flip)
{
    cv::Mat result = image.clone();

    // 应用旋转
    if (rotation != RotationAngle::NONE)
    {
        cv::Point2f center(result.cols / 2.0, result.rows / 2.0);
        cv::Mat rotMatrix;

        switch (rotation)
        {
        case RotationAngle::CW_90:
            cv::rotate(result, result, cv::ROTATE_90_CLOCKWISE);
            break;
        case RotationAngle::CW_180:
            cv::rotate(result, result, cv::ROTATE_180);
            break;
        case RotationAngle::CW_270:
            cv::rotate(result, result, cv::ROTATE_90_COUNTERCLOCKWISE);
            break;
        default:
            break;
        }
    }

    // 应用翻转
    switch (flip)
    {
    case FlipType::HORIZONTAL:
        cv::flip(result, result, 1); // 水平翻转
        break;
    case FlipType::VERTICAL:
        cv::flip(result, result, 0); // 垂直翻转
        break;
    case FlipType::BOTH:
        cv::flip(result, result, -1); // 水平和垂直翻转
        break;
    default:
        break;
    }

    return result;
}

// 根据合并顺序计算瓦片在最终图像中的位置
void calculateTilePosition(const TileInfo &tile, int minX, int minY, int maxX, int maxY,
                           int tileSize, MergeOrder order, int &x, int &y)
{
    switch (order)
    {
    case MergeOrder::TOP_TO_BOTTOM:
        x = (tile.x - minX) * tileSize;
        y = (tile.y - minY) * tileSize;
        break;

    case MergeOrder::LEFT_TO_RIGHT:
        x = (tile.y - minY) * tileSize;
        y = (tile.x - minX) * tileSize;
        break;

    case MergeOrder::BOTTOM_TO_TOP:
        x = (tile.x - minX) * tileSize;
        y = (maxY - tile.y) * tileSize;
        break;

    case MergeOrder::RIGHT_TO_LEFT:
        x = (maxY - tile.y) * tileSize;
        y = (tile.x - minX) * tileSize;
        break;
    }
}

// 合并瓦片
cv::Mat mergeTiles(const std::vector<TileInfo> &tiles, const TransformConfig &transformConfig, int tileSize = 256)
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

    // 根据合并顺序计算合并后图像的尺寸
    int width, height;
    switch (transformConfig.order)
    {
    case MergeOrder::TOP_TO_BOTTOM:
    case MergeOrder::BOTTOM_TO_TOP:
        width = (maxX - minX + 1) * tileSize;
        height = (maxY - minY + 1) * tileSize;
        break;
    case MergeOrder::LEFT_TO_RIGHT:
    case MergeOrder::RIGHT_TO_LEFT:
        width = (maxY - minY + 1) * tileSize;
        height = (maxX - minX + 1) * tileSize;
        break;
    }

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

        // 应用变换到瓦片图像
        cv::Mat transformedTile = applyTransform(tileImg, transformConfig.rotation, transformConfig.flip);

        // 根据合并顺序计算瓦片在合并图像中的位置
        int x, y;
        calculateTilePosition(tile, minX, minY, maxX, maxY, tileSize, transformConfig.order, x, y);

        printf("tileImg: %s, x: %d, y: %d\n", tile.path.c_str(), x, y);

        // 创建目标区域
        cv::Rect roi(x, y, transformedTile.cols, transformedTile.rows);

        // 确保ROI在图像范围内
        if (roi.x >= 0 && roi.y >= 0 && roi.x + roi.width <= result.cols && roi.y + roi.height <= result.rows)
        {
            // 将变换后的瓦片复制到结果图像
            transformedTile.copyTo(result(roi));
        }
    }

    return result;
}

// 合并配置结构体
struct MergeConfig
{
    std::string inputDir;
    std::string outputFile;
    int tileSize;
    TransformConfig transform;
};

// 从配置文件读取合并配置
MergeConfig loadMergeConfig(const std::string &configPath)
{
    INIReader reader(configPath);
    MergeConfig config;

    if (reader.ParseError() < 0)
    {
        printf("Warning: Cannot load config file '%s', using default values\n", configPath.c_str());
        // 使用默认值
        config.inputDir = "tiles";
        config.outputFile = "merged_tiles.jpg";
        config.tileSize = 256;
        config.transform = TransformConfig();
        return config;
    }

    // 读取基本配置
    config.inputDir = reader.Get("merge", "inputDir", "tiles");
    config.outputFile = reader.Get("merge", "outputFile", "merged_tiles.jpg");
    config.tileSize = reader.GetInteger("merge", "tileSize", 256);

    // 读取变换配置
    std::string orderStr = reader.Get("merge", "order", "top-to-bottom");
    if (orderStr == "top-to-bottom")
        config.transform.order = MergeOrder::TOP_TO_BOTTOM;
    else if (orderStr == "left-to-right")
        config.transform.order = MergeOrder::LEFT_TO_RIGHT;
    else if (orderStr == "bottom-to-top")
        config.transform.order = MergeOrder::BOTTOM_TO_TOP;
    else if (orderStr == "right-to-left")
        config.transform.order = MergeOrder::RIGHT_TO_LEFT;
    else
        config.transform.order = MergeOrder::TOP_TO_BOTTOM;

    int rotation = reader.GetInteger("merge", "rotation", 0);
    switch (rotation)
    {
    case 0:
        config.transform.rotation = RotationAngle::NONE;
        break;
    case 90:
        config.transform.rotation = RotationAngle::CW_90;
        break;
    case 180:
        config.transform.rotation = RotationAngle::CW_180;
        break;
    case 270:
        config.transform.rotation = RotationAngle::CW_270;
        break;
    default:
        config.transform.rotation = RotationAngle::NONE;
        break;
    }

    std::string flipStr = reader.Get("merge", "flip", "none");
    if (flipStr == "none")
        config.transform.flip = FlipType::NONE;
    else if (flipStr == "horizontal")
        config.transform.flip = FlipType::HORIZONTAL;
    else if (flipStr == "vertical")
        config.transform.flip = FlipType::VERTICAL;
    else if (flipStr == "both")
        config.transform.flip = FlipType::BOTH;
    else
        config.transform.flip = FlipType::NONE;

    return config;
}

int main(int argc, char *argv[])
{
    // 确定配置文件路径
    std::string configPath = "config.ini";
    if (argc > 1)
    {
        configPath = argv[1];
    }

    printf("Loading configuration from: %s\n", configPath.c_str());

    // 加载配置
    MergeConfig config = loadMergeConfig(configPath);

    printf("Merge Configuration:\n");
    printf("  - Input Directory: %s\n", config.inputDir.c_str());
    printf("  - Output File: %s\n", config.outputFile.c_str());
    printf("  - Tile Size: %d pixels\n", config.tileSize);

    // 打印变换配置信息
    printf("Transform Configuration:\n");
    printf("  - Merge Order: ");
    switch (config.transform.order)
    {
    case MergeOrder::TOP_TO_BOTTOM:
        printf("Top to Bottom\n");
        break;
    case MergeOrder::LEFT_TO_RIGHT:
        printf("Left to Right\n");
        break;
    case MergeOrder::BOTTOM_TO_TOP:
        printf("Bottom to Top\n");
        break;
    case MergeOrder::RIGHT_TO_LEFT:
        printf("Right to Left\n");
        break;
    }

    printf("  - Rotation: ");
    switch (config.transform.rotation)
    {
    case RotationAngle::NONE:
        printf("None (0°)\n");
        break;
    case RotationAngle::CW_90:
        printf("90° Clockwise\n");
        break;
    case RotationAngle::CW_180:
        printf("180°\n");
        break;
    case RotationAngle::CW_270:
        printf("270° Clockwise\n");
        break;
    }

    printf("  - Flip: ");
    switch (config.transform.flip)
    {
    case FlipType::NONE:
        printf("None\n");
        break;
    case FlipType::HORIZONTAL:
        printf("Horizontal\n");
        break;
    case FlipType::VERTICAL:
        printf("Vertical\n");
        break;
    case FlipType::BOTH:
        printf("Both (Horizontal + Vertical)\n");
        break;
    }

    printf("\nScanning tile directory: %s\n", config.inputDir.c_str());

    // 扫描目录获取瓦片信息
    std::vector<TileInfo> tiles = scanTiles(config.inputDir);

    printf("Found %zu tile files\n", tiles.size());

    if (tiles.empty())
    {
        printf("No tile files found, please check directory path\n");
        return 1;
    }

    // 合并瓦片
    cv::Mat mergedImage = mergeTiles(tiles, config.transform, config.tileSize);

    if (mergedImage.empty())
    {
        printf("Failed to merge tiles\n");
        return 1;
    }

    // 保存合并后的图像
    printf("Saving merged image to: %s\n", config.outputFile.c_str());
    cv::imwrite(config.outputFile, mergedImage);

    printf("Tile merge complete!\n");

    // 按任意键继续
    printf("Press any key to exit...\n");
    std::cin.get();

    return 0;
}

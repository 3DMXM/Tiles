#include <opencv2/opencv.hpp>
#include <iostream>
#include <filesystem>
#include <cmath>
#include "INIReader.h" // 添加 inih 库的头文件

int tileSize = 256;
int quality = 90;

void saveTile(const cv::Mat &tile, int z, int x, int y, const std::string &outputDir)
{
    // 创建目录结构 z/x
    std::string zDir = outputDir + "/" + std::to_string(z);
    std::string xDir = zDir + "/" + std::to_string(x);
    std::filesystem::create_directories(xDir);

    // Mapbox 格式的文件名: z/x/y.webp
    std::string fileName = xDir + "/" + std::to_string(y) + ".webp";

    printf("Saving tile in: %s\n", fileName.c_str());

    std::vector<int> params;
    params.push_back(cv::IMWRITE_WEBP_QUALITY);
    params.push_back(quality); // 图片质量 0 to 100
    cv::imwrite(fileName, tile, params);
}

void sliceImage(const std::string &imagePath, int tileSize, const std::string &outputDir)
{
    cv::Mat image = cv::imread(imagePath);

    if (image.empty())
    {
        printf("Could not open or find the image!\n");
        return;
    }

    int originalWidth = image.cols;
    int originalHeight = image.rows;

    // 计算最大缩放级别，Mapbox 通常从 0 级开始
    int maxDimension = std::max(originalWidth, originalHeight);
    int maxZoomLevel = static_cast<int>(std::ceil(std::log2(maxDimension / tileSize)));

    // 计算 0 级的尺寸，即整个世界在一个瓦片中的尺寸
    int baseSize = tileSize;

    printf("Image size: %dx%d, max zoom level: %d\n", originalWidth, originalHeight, maxZoomLevel);

    // 保持原始宽高比
    float aspectRatio = static_cast<float>(originalWidth) / originalHeight;

    for (int z = 0; z <= maxZoomLevel; ++z)
    {
        // 计算当前级别的世界大小（按照 2^z 缩放）
        int worldSizeInTiles = 1 << z; // 2^z
        int worldSizeInPixels = worldSizeInTiles * tileSize;

        // 根据宽高比确定目标图像在世界坐标系中的尺寸
        int targetWidth, targetHeight;
        if (aspectRatio >= 1.0)
        {
            // 宽图
            targetWidth = worldSizeInPixels;
            targetHeight = static_cast<int>(worldSizeInPixels / aspectRatio);
        }
        else
        {
            // 高图
            targetHeight = worldSizeInPixels;
            targetWidth = static_cast<int>(worldSizeInPixels * aspectRatio);
        }

        printf("Zoom level: %d, worldSize: %d tiles (%d pixels), targetResolution: %dx%d\n",
               z, worldSizeInTiles, worldSizeInPixels, targetWidth, targetHeight);

        // 调整图片大小
        cv::Mat zoomedImg;
        cv::resize(image, zoomedImg, cv::Size(targetWidth, targetHeight), 0, 0, cv::INTER_LANCZOS4);

        // 计算需要的瓦片数
        int numTilesX = (targetWidth + tileSize - 1) / tileSize;
        int numTilesY = (targetHeight + tileSize - 1) / tileSize;

        // 确保不超过世界大小
        numTilesX = std::min(numTilesX, worldSizeInTiles);
        numTilesY = std::min(numTilesY, worldSizeInTiles);

        // 计算居中偏移量，使图像在世界坐标系中居中
        int offsetX = (worldSizeInTiles - numTilesX) / 2;
        int offsetY = (worldSizeInTiles - numTilesY) / 2;

        // 切割图片生成瓦片
        int tileCount = 0;
        for (int y = 0; y < numTilesY; ++y)
        {
            for (int x = 0; x < numTilesX; ++x)
            {
                // 确定瓦片区域
                int left = x * tileSize;
                int top = y * tileSize;
                int right = std::min(left + tileSize, targetWidth);
                int bottom = std::min(top + tileSize, targetHeight);

                // 裁剪瓦片
                cv::Rect tileRect(left, top, right - left, bottom - top);
                cv::Mat tile = zoomedImg(tileRect);

                // 补充至标准大小
                if (tile.cols != tileSize || tile.rows != tileSize)
                {
                    cv::Mat paddedTile = cv::Mat::zeros(tileSize, tileSize, tile.type());
                    cv::Rect roi(0, 0, tile.cols, tile.rows);
                    tile.copyTo(paddedTile(roi));
                    tile = paddedTile;
                }

                // 保存瓦片，注意转换为 Mapbox 坐标系（加上偏移量）
                saveTile(tile, z, x + offsetX, y + offsetY, outputDir);
                tileCount++;
            }
        }
        printf("Zoom level: %d, generated %d tiles\n", z, tileCount);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <image_path>" << std::endl;
        return -1;
    }

    // 读取 config.ini 文件
    INIReader reader("config.ini");

    if (reader.ParseError() < 0)
    {
        printf("Can't load 'config.ini' \n");
    }
    else
    {
        tileSize = reader.GetInteger("slice", "tileSize", 256);
        quality = reader.GetInteger("slice", "Quality", 90);

        printf("set tileSize:%d, quality:%d. \n", tileSize, quality);
    }

    std::string imagePath = argv[1];
    std::string outputDir = "out";

    sliceImage(imagePath, tileSize, outputDir);

    // 按任意键继续
    printf("Press any key to continue...\n");
    std::cin.get();

    return 0;
}
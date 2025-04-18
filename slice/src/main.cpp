#include <opencv2/opencv.hpp>
#include <iostream>
#include <filesystem>
#include <cmath>
#include "INIReader.h" // 添加 inih 库的头文件

int tileSize = 256;
int quality = 90;

void saveTile(const cv::Mat &tile, int z, int x, int y, const std::string &outputDir)
{

    std::filesystem::create_directories(outputDir + "/" + std::to_string(z));
    std::string fileName = outputDir + "/" + std::to_string(z) + "/tile_" + std::to_string(x) + "_" + std::to_string(y) + ".webp";

    printf("Saving tile in: %s\n", fileName.c_str());

    std::vector<int> params;
    params.push_back(cv::IMWRITE_WEBP_QUALITY);
    params.push_back(quality); // 图片质量 0 to 100
    cv::imwrite(fileName, tile, params);
}

void sliceImage(const std::string &imagePath, int tileSize, const std::string &outputDir)
{
    int baseResolution = tileSize;

    cv::Mat image = cv::imread(imagePath);

    if (image.empty())
    {
        printf("Could not open or find the image!\n");
        return;
    }

    int originalWidth = image.cols;
    int originalHeight = image.rows;

    // 计算最大缩放级别
    int maxZoomLevel = static_cast<int>(std::log2(std::min(originalWidth, originalHeight) / tileSize)) + 1;

    printf("Image size: %dx%d\n, max zoom level: %d\n", originalWidth, originalHeight, maxZoomLevel);

    for (int z = 0; z <= maxZoomLevel; ++z)
    {
        // int numTiles = std::pow(2, z);
        // int tileWidth = originalWidth / numTiles;
        // int tileHeight = originalHeight / numTiles;

        // 计算当前级别图片的目标分辨率
        int targetResolution = baseResolution * std::pow(2, z);
        // std::cout << "Zoom level " << z << ": 目标分辨率为 " << targetResolution << "x" << targetResolution << "." << std::endl;
        printf("Zoom level: %d, targetResolution: %dx%d\n", z, targetResolution, targetResolution);

        // 调整图片大小到当前级别的分辨率
        cv::Mat zoomedImg;
        cv::resize(image, zoomedImg, cv::Size(targetResolution, targetResolution), 0, 0, cv::INTER_LANCZOS4);
        int zoomWidth = zoomedImg.cols;
        int zoomHeight = zoomedImg.rows;

        // 计算瓦片数量
        int numTilesX = (zoomWidth + tileSize - 1) / tileSize; // 向上取整
        int numTilesY = (zoomHeight + tileSize - 1) / tileSize;

        // 切割当前缩放级别的图片
        int tileCount = 0;
        for (int y = 0; y < numTilesX; ++y)
        {
            for (int x = 0; x < numTilesY; ++x)
            {
                // int xStart = x * tileWidth;
                // int yStart = y * tileHeight;
                // int xEnd = std::min(xStart + tileWidth, originalWidth);
                // int yEnd = std::min(yStart + tileHeight, originalHeight);

                // 确定瓦片的左上角和右下角位置
                int left = x * tileSize;
                int top = y * tileSize;
                int right = std::min(left + tileSize, zoomWidth);
                int bottom = std::min(top + tileSize, zoomHeight);

                // 裁剪瓦片
                cv::Rect tileRect(left, top, right - left, bottom - top);
                cv::Mat tile = zoomedImg(tileRect);

                saveTile(tile, z, x, y, outputDir);
                tileCount++;
            }
        }
        //  std::cout << "Zoom level " << z << ": 切割为 " << tileCount << " 张瓦片，图片分辨率为 " << targetResolution << "x" << targetResolution << "." << std::endl;
        printf("Zoom level: %d, tile have a  %d, The image resolution is %dx%d\n", z, tileCount, targetResolution, targetResolution);
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
    std::cout << "Press any key to continue..." << std::endl;
    std::cin.get();

    return 0;
}
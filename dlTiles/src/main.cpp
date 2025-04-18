#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <curl/curl.h>
#include "INIReader.h"        // 添加 inih 库的头文件
#include <thread>             // 添加线程支持
#include <mutex>              // 添加互斥锁支持
#include <vector>             // 添加向量支持
#include <queue>              // 添加队列支持
#include <condition_variable> // 添加条件变量支持
#include <atomic>             // 添加原子操作支持

namespace fs = std::filesystem;

// 配置参数结构体
struct Config
{
    std::string baseUrl;
    std::string outputDir;
    int zoom;
    int minX;
    int minY;
    int maxX;
    int maxY;
    std::string proxy;
    int threadCount; // 添加线程数量参数
};

// 解析 INI 文件
Config parseConfigFile(const std::string &filePath)
{
    Config config;

    // 读取 config.ini 文件
    INIReader reader(filePath);

    if (reader.ParseError() < 0)
    {
        printf("Can't load 'config.ini' \n");
    }
    else
    {
        config.baseUrl = reader.GetString("download", "baseUrl", "");          // 基础 URL
        config.outputDir = reader.GetString("download", "outputDir", "tiles"); // 输出目录
        config.zoom = reader.GetInteger("download", "zoom", 15);               // 瓦片缩放级别
        config.minX = reader.GetInteger("download", "minX", 0);                // 瓦片范围最小 X 坐标
        config.minY = reader.GetInteger("download", "minY", 0);                // 瓦片范围最小 Y 坐标
        config.maxX = reader.GetInteger("download", "maxX", 0);                // 瓦片范围最大 X 坐标
        config.maxY = reader.GetInteger("download", "maxY", 0);                // 瓦片范围最大 Y 坐标
        config.proxy = reader.GetString("download", "proxy", "");              // HTTP 代理
        config.threadCount = reader.GetInteger("download", "threadCount", 4);  // 线程数量，默认为4
    }

    return config;
}

// 回调函数，用于写入下载的数据到文件
size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    std::ofstream *outFile = static_cast<std::ofstream *>(userp);
    size_t totalSize = size * nmemb;
    outFile->write(static_cast<char *>(contents), totalSize);
    return totalSize;
}

// 任务结构体
struct DownloadTask
{
    std::string url;
    std::string outputPath;
    std::string proxy;
};

// 线程安全的任务队列
class TaskQueue
{
private:
    std::queue<DownloadTask> tasks;
    std::mutex mutex;
    std::condition_variable cv;
    bool done;

public:
    TaskQueue() : done(false) {}

    void addTask(const DownloadTask &task)
    {
        std::unique_lock<std::mutex> lock(mutex);
        tasks.push(task);
        cv.notify_one();
    }

    bool getTask(DownloadTask &task)
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this]
                { return !tasks.empty() || done; });

        if (tasks.empty() && done)
        {
            return false;
        }

        task = tasks.front();
        tasks.pop();
        return true;
    }

    void setDone()
    {
        std::unique_lock<std::mutex> lock(mutex);
        done = true;
        cv.notify_all();
    }

    bool isEmpty()
    {
        std::unique_lock<std::mutex> lock(mutex);
        return tasks.empty();
    }
};

// 下载单个瓦片
bool downloadTile(const std::string &url, const std::string &outputPath, const std::string &proxy = "")
{
    CURL *curl;
    CURLcode res;
    long responseCode;

    curl = curl_easy_init();
    if (curl)
    {
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile.is_open())
        {
            std::cerr << "Unable to open file: " << outputPath << std::endl; // 无法打开文件
            return false;
        }

        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        // 设置HTTP代理（如果有提供）
        if (!proxy.empty())
        {
            curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
        }
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outFile);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            std::cerr << "Download failed: " << curl_easy_strerror(res) << std::endl; // 下载失败
            curl_easy_cleanup(curl);
            return false;
        }

        printf("Downloaded tile: %s, response code: %ld\n", url.c_str(), responseCode);

        curl_easy_cleanup(curl);
        outFile.close();
    }
    else
    {
        curl_easy_cleanup(curl);
    }

    return true;
}

// 工作线程函数
void workerThread(TaskQueue *taskQueue, std::atomic<int> *completedCount, std::atomic<int> *failedCount)
{
    DownloadTask task;
    while (taskQueue->getTask(task))
    {
        if (downloadTile(task.url, task.outputPath, task.proxy))
        {
            (*completedCount)++;
        }
        else
        {
            (*failedCount)++;
            std::cerr << "Failed to download tile: " << task.url << std::endl;
        }
    }
}

// 多线程下载瓦片集
void downloadTiles(const std::string &baseUrl, const std::string &outputDir,
                   int zoom, int minX, int minY, int maxX, int maxY,
                   const std::string &proxy = "", int threadCount = 4)
{
    fs::create_directories(outputDir); // 创建输出目录

    // 创建任务队列
    TaskQueue taskQueue;

    // 计数器
    std::atomic<int> completedCount(0);
    std::atomic<int> failedCount(0);
    int totalTiles = (maxX - minX + 1) * (maxY - minY + 1);

    // 添加所有下载任务到队列
    for (int x = minX; x <= maxX; ++x)
    {
        for (int y = minY; y <= maxY; ++y)
        {
            std::string url = baseUrl;
            url.replace(url.find("{z}"), 3, std::to_string(zoom));
            url.replace(url.find("{x}"), 3, std::to_string(x));
            url.replace(url.find("{y}"), 3, std::to_string(y));

            std::string outputPath = outputDir + "/" + std::to_string(zoom) + "_" + std::to_string(x) + "_" + std::to_string(y) + ".jpg";

            taskQueue.addTask({url, outputPath, proxy});
        }
    }

    // 创建工作线程
    std::vector<std::thread> threads;
    for (int i = 0; i < threadCount; ++i)
    {
        threads.push_back(std::thread(workerThread, &taskQueue, &completedCount, &failedCount));
    }

    // 进度显示
    int lastPercent = 0;
    while (completedCount + failedCount < totalTiles)
    {
        int percent = (completedCount + failedCount) * 100 / totalTiles;
        if (percent > lastPercent)
        {
            std::cout << "\rProgress: " << percent << "% (" << completedCount << " completed, "
                      << failedCount << " failed)" << std::flush;
            lastPercent = percent;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 设置完成标志，确保所有线程可以退出
    taskQueue.setDone();

    // 等待所有线程完成
    for (auto &thread : threads)
    {
        thread.join();
    }

    std::cout << "\rProgress: 100% (" << completedCount << " completed, "
              << failedCount << " failed)" << std::endl;
}

int main()
{
    // 初始化 curl
    curl_global_init(CURL_GLOBAL_ALL);

    // 从配置文件读取参数
    Config config = parseConfigFile("config.ini");

    // 使用配置文件中的参数进行下载
    downloadTiles(
        config.baseUrl,
        config.outputDir,
        config.zoom,
        config.minX,
        config.minY,
        config.maxX,
        config.maxY,
        config.proxy,
        config.threadCount);

    std::cout << "Download completed!" << std::endl;

    // 清理 curl
    curl_global_cleanup();

    return 0;
}
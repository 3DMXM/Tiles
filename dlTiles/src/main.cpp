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

// 检查文件是否存在
bool fileExists(const std::string &filePath)
{
    return fs::exists(filePath);
}

// 下载单个瓦片
bool downloadTile(const std::string &url, const std::string &outputPath, const std::string &proxy = "")
{
    CURL *curl;
    CURLcode res;
    long responseCode = 0;
    const int maxRetries = 2; // 最大重试次数
    int attempts = 0;

    while (attempts <= maxRetries) // 允许初始尝试 + 重试次数
    {
        attempts++;

        curl = curl_easy_init();
        if (curl)
        {
            // 设置请求参数
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            // 设置User-Agent
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36 Edg/135.0.0.0");
            // 设置HTTP代理（如果有提供）
            if (!proxy.empty())
            {
                curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
            }

            // 先不保存响应内容，只检查状态码
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);

            // 执行请求
            res = curl_easy_perform(curl);
            if (res != CURLE_OK)
            {
                fprintf(stderr, "Connection failed: %s\n", curl_easy_strerror(res));
                curl_easy_cleanup(curl);

                if (attempts <= maxRetries)
                {
                    printf("Retrying... Attempt %d of %d\n", attempts, (maxRetries + 1));
                    continue; // 尝试重试
                }
                return false;
            }

            // 获取响应状态码
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

            // 只有状态码为 200 时才下载内容
            if (responseCode == 200)
            {
                // 重置 CURL 选项，准备下载内容
                curl_easy_reset(curl);

                std::ofstream outFile(outputPath, std::ios::binary);
                if (!outFile.is_open())
                {
                    fprintf(stderr, "Unable to open file: %s\n", outputPath.c_str()); // 无法打开文件
                    curl_easy_cleanup(curl);
                    return false;
                }

                // 重新设置请求参数
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                // 设置User-Agent
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36 Edg/135.0.0.0");
                if (!proxy.empty())
                {
                    curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
                }
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outFile);

                // 执行下载
                res = curl_easy_perform(curl);
                outFile.close();

                if (res != CURLE_OK)
                {
                    fprintf(stderr, "Download failed: %s\n", curl_easy_strerror(res));
                    curl_easy_cleanup(curl);

                    if (attempts <= maxRetries)
                    {
                        printf("Retrying download... Attempt %d of %d\n", attempts, (maxRetries + 1));
                        continue; // 尝试重试
                    }
                    return false;
                }

                printf("Downloaded tile: %s, response code: %ld\n", url.c_str(), responseCode);
                curl_easy_cleanup(curl);
                return true; // 下载成功，退出循环
            }
            else
            {
                printf("Skipped tile: %s, response code: %ld\n", url.c_str(), responseCode);
                curl_easy_cleanup(curl);

                if (attempts <= maxRetries && (responseCode >= 500 || responseCode == 429))
                {
                    // 只有在服务器错误(5xx)或请求过多(429)时重试
                    printf("Server error, retrying... Attempt %d of %d\n", attempts, (maxRetries + 1));
                    // 对于429错误可考虑增加延时
                    if (responseCode == 429)
                    {
                        std::this_thread::sleep_for(std::chrono::seconds(2 * attempts)); // 指数退避
                    }
                    continue;
                }
                return false;
            }
        }
        else
        {
            if (attempts <= maxRetries)
            {
                printf("Failed to initialize curl, retrying... Attempt %d of %d\n", attempts, (maxRetries + 1));
                continue; // 尝试重试
            }
            return false;
        }
    }

    return false; // 如果所有尝试都失败
}

// 工作线程函数
void workerThread(TaskQueue *taskQueue, std::atomic<int> *completedCount, std::atomic<int> *failedCount, std::atomic<int> *skippedCount)
{
    DownloadTask task;
    while (taskQueue->getTask(task))
    {
        // 先检查文件是否已存在
        if (fileExists(task.outputPath))
        {
            printf("File already exists: %s\n", task.outputPath.c_str());
            (*skippedCount)++;
            continue;
        }

        // 文件不存在，才执行下载
        if (downloadTile(task.url, task.outputPath, task.proxy))
        {
            (*completedCount)++;
        }
        else
        {
            (*failedCount)++;
            fprintf(stderr, "Failed to download tile: %s\n", task.url.c_str());
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
    std::atomic<int> skippedCount(0); // 新增已存在文件计数器
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
        threads.push_back(std::thread(workerThread, &taskQueue, &completedCount, &failedCount, &skippedCount));
    }

    // 进度显示
    int lastPercent = 0;
    while (completedCount + failedCount + skippedCount < totalTiles)
    {
        int percent = (completedCount + failedCount + skippedCount) * 100 / totalTiles;
        if (percent > lastPercent)
        {
            printf("\rProgress: %d%% (%d completed, %d failed, %d skipped)",
                   percent, completedCount.load(), failedCount.load(), skippedCount.load());
            fflush(stdout); // 替代 std::flush
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

    printf("\rProgress: 100%% (%d completed, %d failed, %d skipped)\n",
           completedCount.load(), failedCount.load(), skippedCount.load());
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

    printf("Download completed!\n");

    // 清理 curl
    curl_global_cleanup();

    return 0;
}
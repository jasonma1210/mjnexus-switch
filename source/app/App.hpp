#pragma once
#include <string>
#include <vector>
#include <mutex>

namespace mjnexus
{
    // App 全局单例：设置、进度存储等
    class App
    {
    public:
        static App& getInstance();

        void setBookPath(const std::string& path) { currentBookPath = path; }
        const std::string& getBookPath() const { return currentBookPath; }

        void setPage(int page) { currentPage = page; }
        int getPage() const { return currentPage; }

    private:
        App() = default;
        std::string currentBookPath;
        int currentPage = 0;
    };

    inline App& App::getInstance()
    {
        static App instance;
        return instance;
    }
}

#pragma once

#include <borealis.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/// Декодер видео, работающий в фоновом потоке.
///
/// libmpv для devkitPro не собрана (см. README, раздел «Видео-трейлеры»):
/// в pacman готового пакета нет, а сборка mpv + патченный FFmpeg с нуля под
/// aarch64-none-elf по рецептам nxmp/SwitchWave требует часы кросс-компиляции
/// и офлайн-патчи, которые нельзя было ни получить, ни проверить в этой
/// среде за разумное время. switch-ffmpeg — готовый пакет из pacman, поэтому
/// плеер написан поверх него напрямую (avformat/avcodec/swscale/swresample).
///
/// Ролик сначала докачивается целиком в файловый кэш на SD (тот же кэш, что
/// у скриншотов), а затем воспроизводится с диска — это проще и надёжнее,
/// чем стриминг чанками поверх нестабильного Wi-Fi.
class VideoDecoder
{
  public:
    VideoDecoder();
    ~VideoDecoder();

    /// Открывает локальный файл и запускает декодирование в фоновом потоке.
    void start(const std::string& localPath);

    /// Останавливает поток и освобождает все ресурсы. Безопасно вызывать
    /// многократно.
    void stop();

    void togglePause();
    bool isPaused() const { return paused; }

    /// Сдвигает позицию воспроизведения. seconds может быть отрицательным.
    void seekBy(double seconds);

    /// Последний декодированный кадр в формате RGBA8. Возвращает true, если
    /// с прошлого вызова появился новый кадр.
    bool takeFrame(std::vector<uint8_t>& outRgba, int& outW, int& outH);

    bool isReady() const { return firstFrameDecoded; }
    bool hasError() const { return error; }

    /// Текущая позиция в секундах — от неё отсчитывается перемотка.
    double position() const { return currentPts; }

  private:
    void decodeLoop(std::string path);

    std::thread worker;
    std::atomic_bool running { false };
    std::atomic_bool paused { false };
    std::atomic_bool firstFrameDecoded { false };
    std::atomic_bool error { false };
    /// Перемотка задаётся абсолютной позицией, а не смещением: смещение
    /// пришлось бы складывать с позицией, которую поток управления не знает.
    std::atomic<double> seekTargetSeconds { 0.0 };
    std::atomic_bool seekRequested { false };
    std::atomic<double> currentPts { 0.0 };

    /// Сон между кадрами прерываемый: иначе stop() из UI-потока ждал бы
    /// до конца текущей задержки, и выход из трейлера подвисал бы.
    std::mutex sleepMutex;
    std::condition_variable sleepCv;
    void interruptibleSleep(int64_t microseconds);

    std::mutex frameMutex;
    std::vector<uint8_t> frameRgba;
    int frameW     = 0;
    int frameH     = 0;
    bool frameDirty = false;
};

/// Вьюха, которая рисует текущий кадр видео поверх nanovg-изображения.
/// Каждый draw() опрашивает декодер: если готов новый кадр, изображение
/// обновляется через nvgUpdateImage (или пересоздаётся, если поменялся
/// размер), иначе рисуется предыдущий кадр — так на экране не мигает
/// чёрный фон между кадрами.
class VideoSurface : public brls::View
{
  public:
    explicit VideoSurface(VideoDecoder* decoder);
    ~VideoSurface() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
        brls::Style style, brls::FrameContext* ctx) override;

    /// Вызывается из draw() с текущим состоянием декодера — по нему activity
    /// показывает или прячет надписи: буферизацию, ошибку и паузу.
    std::function<void(bool loading, bool failed, bool paused)> onState;

  private:
    VideoDecoder* decoder;
    std::vector<uint8_t> scratch;
    int nvgImage = -1;
    int imageW   = 0;
    int imageH   = 0;
};

/// Полноэкранный просмотр видео-трейлера.
///
/// Скачивание идёт через brls::async, декодирование — в собственном потоке
/// внутри VideoDecoder; в UI-поток кадры попадают через takeFrame(),
/// опрашиваемый из VideoSurface::draw(), так что borealis никогда не
/// блокируется. Флаг alive (тот же паттерн, что в remote_image.cpp)
/// защищает колбэк brls::sync от use-after-free, если пользователь успел
/// закрыть экран до завершения загрузки.
class VideoPlayerActivity : public brls::Activity
{
  public:
    explicit VideoPlayerActivity(const std::string& url);
    ~VideoPlayerActivity() override;

    CONTENT_FROM_XML_RES("xml/activity/video_player.xml");

    void onContentAvailable() override;

  private:
    std::string url;
    std::shared_ptr<std::atomic_bool> alive;
    std::unique_ptr<VideoDecoder> decoder;
    VideoSurface* surface = nullptr;

    BRLS_BIND(brls::Box, videoBox, "video/box");
    BRLS_BIND(brls::Label, statusLabel, "video/status");
    BRLS_BIND(brls::Label, pauseLabel, "video/pause");

    void beginDownload();
    void closeSelf();
};

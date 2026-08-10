#pragma once

#include <borealis.hpp>

#include <atomic>
#include <chrono>
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
/// Ролик воспроизводится потоком: показ начинается, как только разобран
/// заголовок, а байты попутно оседают в файловом кэше на SD (тот же кэш, что у
/// скриншотов). Второй просмотр идёт уже с диска и без сети — см. http_stream.hpp.
class VideoDecoder
{
  public:
    VideoDecoder();
    ~VideoDecoder();

    /// Запускает декодирование в фоновом потоке.
    /// Если файл уже в кэше — читается с диска, иначе тянется из сети
    /// потоком, который попутно наполняет кэш (см. http_stream.hpp).
    void start(const std::string& url, const std::string& cachePath,
               std::atomic_bool* alive);

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

    /// Позиция показа и длина ролика в секундах. Длина равна нулю, пока
    /// заголовок не разобран или если её нет в контейнере.
    double position() const { return currentPts.load(); }
    double duration() const { return durationSeconds.load(); }

    /// Доля ролика, уже полученная из сети: 0..1. Для роликов из кэша единица.
    float bufferedFraction() const;
    bool hasError() const { return error; }

    /// Связь оборвалась посреди ролика и идёт попытка продолжить.
    bool isReconnecting() const { return reconnecting; }
    int reconnectAttempt() const { return reconnectTry; }


  private:
    void decodeLoop(std::string url, std::string cachePath, std::atomic_bool* alive);

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
    std::atomic<double> durationSeconds { 0.0 };
    /// Активный сетевой поток, если ролик не из кэша. Нужен, чтобы
    /// сообщить ему о паузе: иначе соединение умирает по таймауту, пока
    /// зритель стоит на паузе.
    std::atomic<class HttpStream*> activeStream { nullptr };
    std::atomic_bool reconnecting { false };
    std::atomic_int reconnectTry { 0 };

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
    std::function<void(bool loading, bool failed, bool paused,
                       bool reconnecting, int attempt)> onState;

    /// Позиция, длительность и доля закачанного. Вызывается не чаще четырёх
    /// раз в секунду — чаще шкала всё равно не меняется заметно.
    std::function<void(double position, double duration, float buffered)> onProgress;

  private:
    /// Прошлое состояние: колбэк зовём только когда что-то изменилось.
    bool lastLoading      = false;
    bool lastFailed       = false;
    bool lastPaused       = false;
    bool lastReconnecting = false;
    int lastAttempt       = -1;

    /// Когда в последний раз сообщали прогресс.
    std::chrono::steady_clock::time_point lastProgress {};

    VideoDecoder* decoder;
    std::vector<uint8_t> scratch;
    int nvgImage = -1;
    int imageW   = 0;
    int imageH   = 0;
};

/// Полноэкранный просмотр видео-трейлера.
///
/// Загрузка идёт в потоке HttpStream, декодирование — в собственном потоке
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

    // «xml/» подставляет сама borealis, см. main.cpp
    CONTENT_FROM_XML_RES("activity/video_player.xml");

    void onContentAvailable() override;

  private:
    std::string url;
    std::shared_ptr<std::atomic_bool> alive;
    std::unique_ptr<VideoDecoder> decoder;
    VideoSurface* surface = nullptr;

    BRLS_BIND(brls::Box, videoBox, "video/box");
    BRLS_BIND(brls::Label, statusLabel, "video/status");
    BRLS_BIND(brls::Box, pauseIcon, "video/pause");
    BRLS_BIND(brls::Box, progressBar, "video/bar");
    BRLS_BIND(brls::Box, track, "video/track");
    BRLS_BIND(brls::Box, loadedBar, "video/loaded");
    BRLS_BIND(brls::Box, playedBar, "video/played");
    BRLS_BIND(brls::Label, elapsedLabel, "video/elapsed");
    BRLS_BIND(brls::Label, leftLabel, "video/left");

    /// Обновляет шкалу и подписи. Зовётся из onProgress не чаще четырёх раз в
    /// секунду: setText помечает раскладку грязной, и делать это на каждом
    /// кадре — та же ошибка, что была с надписью «Пауза».
    void updateProgress(double position, double duration, float buffered);

    /// Что уже нарисовано на шкале: обновляем только изменившееся.
    std::string shownElapsed;
    std::string shownLeft;
    float shownPlayed = -1.0f;
    float shownLoaded = -1.0f;

    void beginDownload();
    void closeSelf();
};

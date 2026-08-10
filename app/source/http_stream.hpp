#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cstdio>
#include <functional>

struct AVIOContext;

/// Чтение файла по HTTP как потока для FFmpeg, с попутной записью в кэш.
///
/// Зачем: раньше ролик скачивался целиком, и только потом начинался показ —
/// на мегабайтах по Wi-Fi это десятки секунд ожидания. Оба условия для
/// стриминга у роликов Cloudinary выполняются: атом moov лежит в начале файла
/// (порядок ftyp moov), поэтому FFmpeg может разобрать поток сразу, а сервер
/// отвечает на Range-запросы (HTTP 206), поэтому возможна перемотка.
///
/// Сеть в самой FFmpeg отключена (--disable-network, --enable-protocol=file):
/// включать её значило бы вернуть в сборку то, что было вырезано ради размера.
/// Вместо этого данные подаются через AVIOContext с колбэками чтения и
/// позиционирования, а качает их тот же curl с mbedtls, который уже слинкован.
///
/// Кэш при этом не теряется: пока файл читается подряд от начала, байты
/// параллельно пишутся во временный файл, и при полном проходе он становится
/// обычным кэшем — второй просмотр открывается мгновенно и без сети. Если была
/// перемотка, дописывание прекращается: склеивать куски из разных диапазонов
/// незачем.
class HttpStream
{
  public:
    HttpStream(std::string url, std::string cachePath, std::atomic_bool* alive);
    ~HttpStream();

    /// AVIOContext, который можно отдать avformat_open_input. Владение
    /// остаётся за HttpStream.
    AVIOContext* avio() { return context; }

    /// Сколько байт получено и сколько всего — для шкалы буферизации.
    /// total() равен -1, пока сервер не сообщил длину.
    long long buffered() const { return received.load(); }
    long long total() const { return contentLength.load(); }

    /// Связь оборвалась посреди файла и мы пробуем продолжить с того же
    /// места. Показ при этом замирает на последнем кадре, а не падает.
    bool reconnecting() const { return isReconnecting.load(); }

    /// Вызывается при смене состояния переподключения — чтобы плеер мог
    /// сказать об этом пользователю.
    std::function<void(bool reconnecting, int attempt)> onReconnect;

    /// Пока показ на паузе, читатель не забирает данные и буфер стоит
    /// полным. Без этой подсказки соединение обрывалось по таймауту
    /// низкой скорости, и попытки переподключения тратились впустую —
    /// пауза дольше пары минут убивала ролик.
    void setReaderPaused(bool paused);

    /// Нужны колбэкам curl, которые не могут быть членами класса.
    size_t onDataPublic(const uint8_t* data, size_t size);
    size_t onHeaderPublic(const char* line, size_t size);

    /// Загрузку пора бросить: поток останавливают или плеер уже закрыт.
    /// Спрашивает колбэк хода передачи, чтобы оборвать запрос сразу, а не по
    /// таймауту.
    bool cancelled() const { return !workerRunning.load() || !alive->load(); }

  private:
    static int readPacket(void* opaque, uint8_t* buf, int size);
    static int64_t seek(void* opaque, int64_t offset, int whence);

    void startWorker(int64_t from);
    /// Один запрос диапазона. Возвращает код curl.
    int performRange(int64_t from);
    /// Отдаёт данные из уже готового файла в кэше вместо сети.
    void feedFromCache(int64_t from);
    void stopWorker();
    size_t onData(const uint8_t* data, size_t size);

    std::string url;
    std::string cachePath;
    std::atomic_bool* alive;

    AVIOContext* context = nullptr;
    unsigned char* avioBuffer = nullptr;

    std::thread worker;
    std::atomic_bool workerRunning { false };
    std::atomic_bool connectionFailed { false };
    std::atomic_bool finished { false };
    std::atomic_bool isReconnecting { false };
    /// сколько байт принято в текущем запросе — по нему считаем, с какого
    /// места продолжать после обрыва
    std::atomic<long long> requestReceived { 0 };
    std::atomic_bool readerPaused { false };
    /// сервер вернул 200 вместо 206 на запрос диапазона — дописывать
    /// такой ответ в конец буфера нельзя, это склеит файл с самим собой
    std::atomic_bool rangeIgnored { false };
    /// ждём ли мы сейчас диапазон (from > 0)
    bool expectPartial = false;
    std::atomic<long long> received { 0 };
    std::atomic<long long> contentLength { -1 };

    mutable std::mutex mutex;
    std::condition_variable cv;
    std::vector<uint8_t> buffer;  ///< данные, начиная с файлового смещения base
    int64_t base     = 0;
    int64_t position = 0;

    // запись в кэш идёт, только пока чтение шло подряд с нуля
    std::FILE* cacheFile = nullptr;
    std::string cacheTmp;
    bool cacheAllowed = true;
    std::atomic_bool cacheComplete { false };

    void closeCache(bool keep);
};

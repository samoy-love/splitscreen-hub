#include "http_stream.hpp"

#include <borealis.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <thread>

#include <curl/curl.h>

extern "C"
{
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

namespace
{

/// Сколько данных держим впереди позиции чтения. Больше — устойчивее к
/// провалам связи, но и памяти в applet-режиме мало.
constexpr size_t READ_AHEAD_LIMIT = 6 * 1024 * 1024;

/// Размер буфера, который AVIOContext просит заполнять за раз.
constexpr int AVIO_BUFFER_SIZE = 64 * 1024;

/// Сколько раз пробуем продолжить после обрыва. Дальше честнее
/// показать ошибку, чем держать зрителя перед замершим кадром.
constexpr int MAX_RECONNECTS = 5;

std::string lowercase(const std::string& text)
{
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

size_t curlHeader(char* line, size_t size, size_t count, void* userdata)
{
    auto* stream = static_cast<HttpStream*>(userdata);
    return stream ? stream->onHeaderPublic(line, size * count) : 0;
}

size_t curlWrite(void* chunk, size_t size, size_t count, void* userdata)
{
    auto* stream = static_cast<HttpStream*>(userdata);
    return stream ? stream->onDataPublic(static_cast<const uint8_t*>(chunk), size * count) : 0;
}

}  // namespace

// onData объявлен приватным, но нужен колбэку curl — пробрасываем.
size_t HttpStream::onDataPublic(const uint8_t* data, size_t size)
{
    return onData(data, size);
}

size_t HttpStream::onHeaderPublic(const char* line, size_t size)
{
    // Заголовки HTTP регистронезависимы: HTTP/2 присылает их строчными.
    // Сравнение по точному написанию однажды молча перестало бы работать,
    // а симптомом был бы обрезанный ролик, осевший в кэше.
    const std::string header = lowercase(std::string(line, size));

    if (header.compare(0, 5, "http/") == 0)
    {
        // На запрос диапазона обязан прийти 206. Если сервер прислал 200,
        // он проигнорировал Range и отдаёт файл сначала — такие байты
        // нельзя дописывать в конец буфера.
        const size_t space = header.find(' ');
        const int code = space == std::string::npos ? 0 : std::atoi(header.c_str() + space + 1);
        if (expectPartial && code == 200)
            rangeIgnored = true;
    }

    const size_t slash = header.rfind('/');
    if (header.compare(0, 14, "content-range:") == 0 && slash != std::string::npos)
        contentLength = std::atoll(header.c_str() + slash + 1);

    return size;
}

void HttpStream::setReaderPaused(bool paused)
{
    readerPaused = paused;
    cv.notify_all();
}

HttpStream::HttpStream(std::string url, std::string cachePath, std::atomic_bool* alive)
    : url(std::move(url))
    , cachePath(std::move(cachePath))
    , alive(alive)
{
    cacheTmp = this->cachePath + ".part";

    avioBuffer = static_cast<unsigned char*>(av_malloc(AVIO_BUFFER_SIZE));
    context    = avio_alloc_context(avioBuffer, AVIO_BUFFER_SIZE, 0, this,
                                    &HttpStream::readPacket, nullptr, &HttpStream::seek);

    startWorker(0);
}

HttpStream::~HttpStream()
{
    stopWorker();
    closeCache(false);

    if (context)
    {
        // avio мог подменить буфер, освобождаем актуальный
        av_freep(&context->buffer);
        avio_context_free(&context);
    }
}

void HttpStream::closeCache(bool keep)
{
    if (cacheFile)
    {
        std::fclose(cacheFile);
        cacheFile = nullptr;
    }

    if (keep)
    {
        std::remove(cachePath.c_str());
        if (std::rename(cacheTmp.c_str(), cachePath.c_str()) == 0)
            cacheComplete = true;
    }
    else if (!cacheComplete.load())
    {
        std::remove(cacheTmp.c_str());
    }
}

size_t HttpStream::onData(const uint8_t* data, size_t size)
{
    if (!alive->load() || rangeIgnored.load())
        return 0;  // ноль обрывает передачу

    // попутная запись в кэш, пока читаем подряд с начала файла
    if (cacheAllowed && cacheFile)
        std::fwrite(data, 1, size, cacheFile);

    std::unique_lock<std::mutex> lock(mutex);

    // придерживаем закачку, если ушли слишком далеко вперёд от читателя
    while (workerRunning.load() && alive->load()
           && buffer.size() > READ_AHEAD_LIMIT + static_cast<size_t>(position - base))
    {
        cv.wait_for(lock, std::chrono::milliseconds(50));
    }
    if (!workerRunning.load() || !alive->load())
        return 0;

    buffer.insert(buffer.end(), data, data + size);
    received += static_cast<long long>(size);
    requestReceived += static_cast<long long>(size);
    cv.notify_all();
    return size;
}

void HttpStream::startWorker(int64_t from)
{
    stopWorker();

    {
        std::lock_guard<std::mutex> lock(mutex);
        buffer.clear();
        base     = from;
        position = from;
        finished = false;
    }

    // Ролик уже целиком лежит в кэше — читаем оттуда. Без этой ветки
    // зацикленный трейлер на каждом обороте перекачивался из сети заново и по
    // дороге затирал готовый файл: closeCache(true) удаляет цель перед
    // переименованием. Получался бесконечный трафик и постоянная чистка кэша.
    if (cacheComplete.load())
    {
        cacheAllowed  = false;
        workerRunning = true;
        worker        = std::thread([this, from]() { feedFromCache(from); });
        return;
    }

    // кэшируем только полный проход с нуля
    cacheAllowed = (from == 0);
    if (cacheAllowed && !cacheFile)
        cacheFile = std::fopen(cacheTmp.c_str(), "wb");
    if (!cacheAllowed)
        closeCache(false);

    workerRunning = true;
    worker        = std::thread([this, from]() { feedFromNetwork(from); });
}

void HttpStream::feedFromNetwork(int64_t from)
{
    int64_t offset  = from;
    int attempt     = 0;

    while (workerRunning.load() && alive->load())
    {
        requestReceived = 0;
        const int result = performRange(offset);
        offset += requestReceived.load();

        const long long length = contentLength.load();
        const bool complete    = length > 0 && offset >= length;

        // Дочитали до конца — либо по известной длине, либо сервер сам
        // закрыл соединение без ошибки и длины мы не знаем.
        if (complete || (result == 0 && length <= 0))
        {
            brls::Logger::info("поток: файл дочитан, {} Б, кеширование {}", offset,
                               cacheAllowed ? "включено" : "выключено");
            if (cacheAllowed)
                closeCache(true);
            break;
        }

        if (!workerRunning.load() || !alive->load())
            break;

        if (rangeIgnored.load())
        {
            // сервер не поддержал докачку — продолжать нечем
            brls::Logger::error("поток: сервер ответил 200 вместо 206, докачка невозможна");
            connectionFailed = true;
            break;
        }

        // Пауза показа — не обрыв связи: читатель просто перестал
        // забирать данные, буфер встал полным, и соединение умерло по
        // таймауту. Ждём снятия паузы, не тратя попыток.
        if (readerPaused.load())
        {
            while (readerPaused.load() && workerRunning.load() && alive->load())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Обрыв посреди файла: продолжаем с того места, где остановились.
        // Кэш при этом не портится — байты по-прежнему идут подряд.
        if (++attempt > MAX_RECONNECTS)
        {
            brls::Logger::error("поток: исчерпаны {} попыток переподключения на байте {}",
                                MAX_RECONNECTS, offset);
            if (offset == from)
                connectionFailed = true;
            break;
        }

        isReconnecting = true;
        if (onReconnect)
            onReconnect(true, attempt);

        // нарастающая пауза, но не дольше трёх секунд: ролик ждать нельзя
        for (int slept = 0; slept < std::min(500 * attempt, 3000)
             && workerRunning.load() && alive->load(); slept += 100)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        isReconnecting = false;
        if (onReconnect)
            onReconnect(false, attempt);
    }

    finished = true;
    cv.notify_all();
}

void HttpStream::feedFromCache(int64_t from)
{
    brls::Logger::debug("поток: читаем из кэша с байта {} — {}", from, cachePath);

    std::FILE* file = std::fopen(cachePath.c_str(), "rb");
    if (!file)
    {
        // Файл мог исчезнуть между проверкой и открытием — например, его убрала
        // чистка кэша. Раньше здесь стояло finished = true: читатель получал
        // EOF, и плеер показывал ошибку вместо ролика, хотя комментарий рядом
        // обещал перекачать. Теперь действительно перекачиваем.
        brls::Logger::warning("поток: кэш пропал, возвращаемся к сети");
        cacheComplete = false;
        cacheAllowed  = (from == 0);
        if (cacheAllowed && !cacheFile)
            cacheFile = std::fopen(cacheTmp.c_str(), "wb");
        feedFromNetwork(from);
        return;
    }

    std::fseek(file, 0, SEEK_END);
    contentLength = std::ftell(file);
    std::fseek(file, static_cast<long>(from), SEEK_SET);

    std::vector<uint8_t> chunk(64 * 1024);
    while (workerRunning.load() && alive->load())
    {
        const size_t got = std::fread(chunk.data(), 1, chunk.size(), file);
        if (got == 0)
            break;
        if (onData(chunk.data(), got) == 0)
            break;  // читателя больше нет
    }

    std::fclose(file);
    finished = true;
    cv.notify_all();
}

int HttpStream::performRange(int64_t from)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        return -1;

    expectPartial = from > 0;
    rangeIgnored  = false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, this);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 512L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "splitscreen-hub/1.0");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);

    // Отмена без ожидания. Раньше закрытие плеера ждало, пока curl сам заметит
    // обрыв: на зависшем Wi-Fi это до двадцати секунд по LOW_SPEED_TIME, и всё
    // это время интерфейс не рисовал кадров. Возврат ненуля обрывает передачу
    // немедленно.
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
                     +[](void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                         return static_cast<HttpStream*>(userdata)->cancelled() ? 1 : 0;
                     });

    // Диапазон запрашиваем всегда, даже с нуля: только в ответе 206 есть
    // Content-Range с полным размером файла. Без него на первом проходе
    // размер оставался неизвестным, и обрыв на середине было не отличить
    // от конца файла — ролик молча обрезался и таким попадал в кэш.
    curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(from));

    const CURLcode result = curl_easy_perform(curl);

    if (contentLength.load() < 0)
    {
        curl_off_t length = 0;
        if (curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length) == CURLE_OK
            && length > 0)
        {
            contentLength = static_cast<long long>(length) + from;
        }
    }

    curl_easy_cleanup(curl);
    return static_cast<int>(result);
}

void HttpStream::stopWorker()
{
    workerRunning = false;
    cv.notify_all();
    if (worker.joinable())
        worker.join();
}

int HttpStream::readPacket(void* opaque, uint8_t* buf, int size)
{
    auto* self = static_cast<HttpStream*>(opaque);

    std::unique_lock<std::mutex> lock(self->mutex);
    self->cv.wait(lock, [self, size] {
        const int64_t available = self->base + static_cast<int64_t>(self->buffer.size())
            - self->position;
        return available > 0 || self->finished.load() || !self->alive->load();
    });

    if (!self->alive->load())
        return AVERROR_EOF;

    const int64_t offset    = self->position - self->base;
    const int64_t available = static_cast<int64_t>(self->buffer.size()) - offset;
    if (available <= 0)
        return AVERROR_EOF;

    const int take = static_cast<int>(std::min<int64_t>(size, available));
    std::memcpy(buf, self->buffer.data() + offset, static_cast<size_t>(take));
    self->position += take;

    // отпускаем прочитанное, чтобы буфер не рос бесконечно
    // Сдвиг двигает весь остаток, поэтому делаем его редко: когда прочитана
    // больше половины буфера. Так перекладывание амортизируется.
    const int64_t consumed = self->position - self->base;
    if (consumed > static_cast<int64_t>(self->buffer.size()) / 2 && consumed > 1024 * 1024)
    {
        self->buffer.erase(self->buffer.begin(),
                           self->buffer.begin() + static_cast<size_t>(consumed));
        self->base = self->position;
    }

    self->cv.notify_all();
    return take;
}

int64_t HttpStream::seek(void* opaque, int64_t offset, int whence)
{
    auto* self = static_cast<HttpStream*>(opaque);

    if (whence == AVSEEK_SIZE)
        return self->contentLength.load();

    int64_t target = offset;
    if (whence == SEEK_CUR)
    {
        std::lock_guard<std::mutex> lock(self->mutex);
        target = self->position + offset;
    }
    else if (whence == SEEK_END)
    {
        const long long length = self->contentLength.load();
        if (length < 0)
            return -1;
        target = length + offset;
    }

    {
        std::lock_guard<std::mutex> lock(self->mutex);
        // цель уже в буфере — просто двигаем указатель, без нового запроса
        if (target >= self->base
            && target <= self->base + static_cast<int64_t>(self->buffer.size()))
        {
            self->position = target;
            return target;
        }
    }

    // ушли за пределы буфера: перезапрашиваем файл с нужного места
    self->startWorker(target);
    return target;
}

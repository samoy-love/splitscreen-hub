#include "ui/video_player.hpp"

#include "net.hpp"
#include "tasks.hpp"

#include <borealis.hpp>

#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <fstream>

#include <curl/curl.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <SDL2/SDL.h>

namespace
{

/// Видео заметно больше скриншота, поэтому у него свой таймаут — общий
/// net::fetch() режет соединение через 20с, ролику на 30-60с этого мало.
size_t writeToFile(void* chunk, size_t size, size_t count, void* userdata)
{
    auto* out           = static_cast<std::ofstream*>(userdata);
    const size_t total  = size * count;
    out->write(static_cast<char*>(chunk), static_cast<std::streamsize>(total));
    return out->good() ? total : 0;
}

struct Progress
{
    std::atomic_bool* alive;
    std::function<void(long long done, long long total)> report;
    int64_t lastReportUs = 0;
};

/// curl зовёт это по ходу скачивания. Заодно единственное место, где можно
/// оборвать закачку: раньше уход с экрана не останавливал её, и тяжёлый поток
/// оставался занят до конца файла.
int onProgress(void* userdata, curl_off_t total, curl_off_t done, curl_off_t, curl_off_t)
{
    auto* p = static_cast<Progress*>(userdata);
    if (!p->alive->load())
        return 1;  // ненулевое значение прерывает передачу

    // не чаще пяти раз в секунду: каждый отчёт идёт через brls::sync
    const int64_t now = av_gettime();
    if (now - p->lastReportUs < 200000)
        return 0;
    p->lastReportUs = now;

    if (p->report)
        p->report(static_cast<long long>(done), static_cast<long long>(total));
    return 0;
}

/// Есть ли ролик уже в кэше — тогда показывать «загрузку» незачем.
bool isCached(const std::string& url)
{
    std::ifstream probe(net::cachePath(url), std::ios::binary | std::ios::ate);
    return probe.good() && probe.tellg() > 0;
}

/// Качает файл целиком в кэш на SD (тот же каталог, что у скриншотов).
/// Возвращает путь к готовому файлу или пустую строку при неудаче.
enum class Failure
{
    None,
    Network,    // обрыв, таймаут — повтор имеет смысл
    Server,     // 4xx — ролика нет, повторять бесполезно
    Cancelled,  // пользователь ушёл с экрана
};

std::string downloadVideo(const std::string& url, std::atomic_bool* alive,
                          std::function<void(long long, long long)> report,
                          Failure* failure = nullptr)
{
    const std::string path = net::cachePath(url);

    if (isCached(url))
        return path;

    // сеть могла ещё не подняться: она инициализируется в фоне
    if (!net::isReady() && !net::waitReady(5000))
        return {};

    const std::string tmp = path + ".part";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out.good())
        return {};

    CURL* curl = curl_easy_init();
    if (!curl)
        return {};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    // жёсткого лимита на всё соединение нет — ролик докачивается, сколько
    // потребуется, а не обрывается на середине как скриншот
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "couch-coop/1.0");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    Progress progress { alive, std::move(report), 0 };
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);

    CURLcode result = curl_easy_perform(curl);
    long status     = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    out.close();

    if (!alive->load() || result != CURLE_OK || status != 200)
    {
        std::remove(tmp.c_str());
        // «сервер отказал» и «связь оборвалась» — разные случаи: первый
        // повторять незачем, второй почти всегда лечится повтором
        if (failure)
            *failure = (result == CURLE_ABORTED_BY_CALLBACK || !alive->load())
                ? Failure::Cancelled
                : (status >= 400 && status < 500 ? Failure::Server : Failure::Network);
        return {};
    }

    std::remove(path.c_str());
    std::rename(tmp.c_str(), path.c_str());
    net::trimCache();
    return path;
}

}  // namespace

// ---------------------------------------------------------------------------
// VideoDecoder
// ---------------------------------------------------------------------------

VideoDecoder::VideoDecoder() = default;

VideoDecoder::~VideoDecoder()
{
    stop();
}

void VideoDecoder::start(const std::string& localPath)
{
    stop();
    running = true;
    worker  = std::thread(&VideoDecoder::decodeLoop, this, localPath);
}

void VideoDecoder::stop()
{
    running = false;
    sleepCv.notify_all();  // будим поток, если он спит между кадрами
    if (worker.joinable())
        worker.join();
}

void VideoDecoder::interruptibleSleep(int64_t microseconds)
{
    if (microseconds <= 0)
        return;
    std::unique_lock<std::mutex> lock(sleepMutex);
    sleepCv.wait_for(lock, std::chrono::microseconds(microseconds),
                     [this] { return !running.load() || seekRequested.load(); });
}

void VideoDecoder::togglePause()
{
    paused = !paused.load();
}

void VideoDecoder::seekBy(double seconds)
{
    // считаем цель здесь, от фактической позиции: декодер её знает, а
    // вызывающий — нет
    double target = currentPts.load() + seconds;
    seekTargetSeconds = target < 0 ? 0 : target;
    seekRequested     = true;
}

bool VideoDecoder::takeFrame(std::vector<uint8_t>& outRgba, int& outW, int& outH)
{
    std::lock_guard<std::mutex> lock(frameMutex);
    if (!frameDirty)
        return false;
    // обмен вместо копии: буфер вызывающего уходит декодеру и переиспользуется
    // под следующий кадр. При 1280x720 это 3.5 МБ, которые незачем копировать
    // по два раза на кадр.
    outRgba.swap(frameRgba);
    outW       = frameW;
    outH       = frameH;
    frameDirty = false;
    return true;
}

void VideoDecoder::decodeLoop(std::string path)
{
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0)
    {
        error = true;
        return;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0)
    {
        avformat_close_input(&fmt);
        error = true;
        return;
    }

    int videoIdx = -1, audioIdx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; i++)
    {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoIdx < 0)
            videoIdx = static_cast<int>(i);
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioIdx < 0)
            audioIdx = static_cast<int>(i);
    }

    if (videoIdx < 0)
    {
        avformat_close_input(&fmt);
        error = true;
        return;
    }

    AVCodecParameters* vpar = fmt->streams[videoIdx]->codecpar;
    const AVCodec* vcodec   = avcodec_find_decoder(vpar->codec_id);
    AVCodecContext* vctx    = vcodec ? avcodec_alloc_context3(vcodec) : nullptr;
    if (!vctx || avcodec_parameters_to_context(vctx, vpar) < 0 || avcodec_open2(vctx, vcodec, nullptr) < 0)
    {
        if (vctx)
            avcodec_free_context(&vctx);
        avformat_close_input(&fmt);
        error = true;
        return;
    }

    AVCodecContext* actx  = nullptr;
    SwrContext* swr       = nullptr;
    SDL_AudioDeviceID dev = 0;
    int outChannels       = 2;

    if (audioIdx >= 0)
    {
        AVCodecParameters* apar = fmt->streams[audioIdx]->codecpar;
        const AVCodec* acodec   = avcodec_find_decoder(apar->codec_id);
        if (acodec)
        {
            actx = avcodec_alloc_context3(acodec);
            if (actx && avcodec_parameters_to_context(actx, apar) == 0
                && avcodec_open2(actx, acodec, nullptr) == 0)
            {
                swr = swr_alloc();
                av_opt_set_chlayout(swr, "in_chlayout", &actx->ch_layout, 0);
                av_opt_set_int(swr, "in_sample_rate", actx->sample_rate, 0);
                av_opt_set_sample_fmt(swr, "in_sample_fmt", actx->sample_fmt, 0);

                AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
                av_opt_set_chlayout(swr, "out_chlayout", &outLayout, 0);
                av_opt_set_int(swr, "out_sample_rate", actx->sample_rate, 0);
                av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                swr_init(swr);

                SDL_AudioSpec want {}, have {};
                want.freq     = actx->sample_rate;
                want.format   = AUDIO_S16SYS;
                want.channels = outChannels;
                want.samples  = 1024;
                dev           = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
                if (dev)
                    SDL_PauseAudioDevice(dev, 0);
            }
            else if (actx)
            {
                avcodec_free_context(&actx);
            }
        }
    }

    SwsContext* sws  = nullptr;
    int scaledW = 0, scaledH = 0;
    AVFrame* frame    = av_frame_alloc();
    AVFrame* rgbFrame = av_frame_alloc();
    AVPacket* packet  = av_packet_alloc();

    const AVRational tb = fmt->streams[videoIdx]->time_base;

    // Часы воспроизведения. Кадр с меткой pts должен быть показан в момент
    // clockStartUs + pts. Всё, что сдвигает воспроизведение во времени —
    // перемотка, пауза, зацикливание — двигает clockStartUs, иначе
    // рассинхрон копится: после паузы реальное время уходит вперёд, условие
    // ожидания перестаёт срабатывать и ролик проматывается на максимальной
    // скорости, пока не догонит.
    int64_t clockStartUs = av_gettime();

    auto restartClock = [&clockStartUs](double positionSeconds) {
        clockStartUs = av_gettime() - static_cast<int64_t>(positionSeconds * 1000000.0);
    };

    // сбрасывает всё, что накопили декодеры и звуковая очередь: иначе после
    // перемотки ещё несколько секунд играет звук со старой позиции
    auto flushAfterJump = [&](double positionSeconds) {
        avcodec_flush_buffers(vctx);
        if (actx)
            avcodec_flush_buffers(actx);
        if (dev)
            SDL_ClearQueuedAudio(dev);
        currentPts = positionSeconds;
        restartClock(positionSeconds);
    };

    while (running.load())
    {
        if (seekRequested.exchange(false))
        {
            double target = seekTargetSeconds.load();
            int64_t ts    = static_cast<int64_t>(target / av_q2d(tb));
            av_seek_frame(fmt, videoIdx, ts, AVSEEK_FLAG_BACKWARD);
            flushAfterJump(target);
        }

        if (paused.load())
        {
            const int64_t pauseBegan = av_gettime();
            while (paused.load() && running.load() && !seekRequested.load())
                interruptibleSleep(30000);
            // простой не должен считаться отставанием
            clockStartUs += av_gettime() - pauseBegan;
            continue;
        }

        int readResult = av_read_frame(fmt, packet);
        if (readResult < 0)
        {
            // ролик закончился — начинаем сначала, это трейлер в цикле
            av_seek_frame(fmt, videoIdx, 0, AVSEEK_FLAG_BACKWARD);
            flushAfterJump(0.0);
            continue;
        }

        if (packet->stream_index == videoIdx)
        {
            if (avcodec_send_packet(vctx, packet) == 0)
            {
                while (avcodec_receive_frame(vctx, frame) == 0)
                {
                    // Разрешение потока может смениться на ходу (так бывает в
                    // мультибитрейтных mp4) — тогда старый буфер меньше нового
                    // кадра, и запись уйдёт за его границу.
                    if (sws && (frame->width != scaledW || frame->height != scaledH))
                    {
                        sws_freeContext(sws);
                        sws = nullptr;
                        av_freep(&rgbFrame->data[0]);
                    }

                    if (!sws)
                    {
                        sws = sws_getContext(frame->width, frame->height,
                            static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
                            AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
                        av_image_alloc(rgbFrame->data, rgbFrame->linesize, frame->width,
                            frame->height, AV_PIX_FMT_RGBA, 1);
                        scaledW = frame->width;
                        scaledH = frame->height;
                    }

                    if (sws)
                    {
                        sws_scale(sws, frame->data, frame->linesize, 0, frame->height,
                            rgbFrame->data, rgbFrame->linesize);

                        std::lock_guard<std::mutex> lock(frameMutex);
                        const size_t bytes = static_cast<size_t>(rgbFrame->linesize[0]) * frame->height;
                        frameRgba.assign(rgbFrame->data[0], rgbFrame->data[0] + bytes);
                        frameW     = frame->width;
                        frameH     = frame->height;
                        frameDirty = true;
                    }

                    firstFrameDecoded = true;

                    // ждём момента, когда кадр должен появиться на экране
                    double pts = frame->best_effort_timestamp * av_q2d(tb);
                    if (pts < 0)
                        pts = currentPts.load();
                    currentPts = pts;

                    int64_t dueUs   = clockStartUs + static_cast<int64_t>(pts * 1000000.0);
                    int64_t delayUs = dueUs - av_gettime();
                    // спим только вперёд и не больше секунды: битая метка
                    // времени не должна подвесить поток на минуты
                    if (delayUs > 0)
                        interruptibleSleep(delayUs > 1000000 ? 1000000 : delayUs);
                    if (!running.load())
                        break;
                }
            }
        }
        else if (actx && packet->stream_index == audioIdx)
        {
            if (avcodec_send_packet(actx, packet) == 0)
            {
                while (avcodec_receive_frame(actx, frame) == 0)
                {
                    uint8_t* outBuf[2] = { nullptr, nullptr };
                    int outSamples     = static_cast<int>(av_rescale_rnd(
                        swr_get_delay(swr, actx->sample_rate) + frame->nb_samples, actx->sample_rate,
                        actx->sample_rate, AV_ROUND_UP));
                    int lineSize = 0;
                    av_samples_alloc(outBuf, &lineSize, outChannels, outSamples, AV_SAMPLE_FMT_S16, 0);
                    int converted = swr_convert(swr, outBuf, outSamples,
                        const_cast<const uint8_t**>(frame->data), frame->nb_samples);
                    if (converted > 0 && dev)
                    {
                        SDL_QueueAudio(dev, outBuf[0],
                            static_cast<Uint32>(converted * outChannels * sizeof(int16_t)));
                    }
                    av_freep(&outBuf[0]);
                }
            }
        }

        av_packet_unref(packet);
    }

    if (dev)
        SDL_CloseAudioDevice(dev);
    if (swr)
        swr_free(&swr);
    if (sws)
        sws_freeContext(sws);
    if (rgbFrame->data[0])
        av_freep(&rgbFrame->data[0]);
    av_frame_free(&frame);
    av_frame_free(&rgbFrame);
    av_packet_free(&packet);
    if (actx)
        avcodec_free_context(&actx);
    avcodec_free_context(&vctx);
    avformat_close_input(&fmt);
}

// ---------------------------------------------------------------------------
// VideoSurface
// ---------------------------------------------------------------------------

VideoSurface::VideoSurface(VideoDecoder* decoder)
    : decoder(decoder)
{
    this->setGrow(1.0f);
}

VideoSurface::~VideoSurface()
{
    if (nvgImage >= 0)
        nvgDeleteImage(brls::Application::getNVGContext(), nvgImage);
}

void VideoSurface::draw(NVGcontext* vg, float x, float y, float width, float height,
    brls::Style style, brls::FrameContext* ctx)
{
    int w = 0, h = 0;
    if (decoder && decoder->takeFrame(scratch, w, h) && w > 0 && h > 0)
    {
        if (nvgImage < 0 || w != imageW || h != imageH)
        {
            if (nvgImage >= 0)
                nvgDeleteImage(vg, nvgImage);
            nvgImage = nvgCreateImageRGBA(vg, w, h, 0, scratch.data());
            imageW   = w;
            imageH   = h;
        }
        else
        {
            nvgUpdateImage(vg, nvgImage, scratch.data());
        }
    }

    if (onState)
    {
        bool loading = decoder && !decoder->isReady() && !decoder->hasError();
        bool failed  = decoder && decoder->hasError();
        bool paused  = decoder && decoder->isPaused();
        onState(loading, failed, paused);
    }

    if (nvgImage < 0)
        return;  // ещё ничего не декодировано — статус-лейбл показывает индикатор поверх

    NVGpaint paint = nvgImagePattern(vg, x, y, width, height, 0, nvgImage, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
}

// ---------------------------------------------------------------------------
// VideoPlayerActivity
// ---------------------------------------------------------------------------

VideoPlayerActivity::VideoPlayerActivity(const std::string& url)
    : url(url)
    , alive(std::make_shared<std::atomic_bool>(true))
{
}

VideoPlayerActivity::~VideoPlayerActivity()
{
    *alive = false;
    if (decoder)
        decoder->stop();
}

void VideoPlayerActivity::onContentAvailable()
{
    statusLabel->setText(isCached(url) ? "Открываем трейлер…" : "Загрузка трейлера…");
    statusLabel->setVisibility(brls::Visibility::VISIBLE);

    this->registerAction("Закрыть", brls::BUTTON_B, [this](brls::View*) {
        closeSelf();
        return true;
    });
    this->registerAction("Пауза", brls::BUTTON_A, [this](brls::View*) {
        if (decoder)
            decoder->togglePause();
        return true;
    });
    this->registerAction("Назад 10с", brls::BUTTON_LEFT, [this](brls::View*) {
        if (decoder)
            decoder->seekBy(-10.0);
        return true;
    });
    this->registerAction("Вперёд 10с", brls::BUTTON_RIGHT, [this](brls::View*) {
        if (decoder)
            decoder->seekBy(10.0);
        return true;
    });

    beginDownload();
}

void VideoPlayerActivity::beginDownload()
{
    auto flag    = alive;
    std::string u = url;

    tasks::heavy([this, flag, u]() {
        // отчёт о прогрессе идёт в UI-поток: без него на весь файл висела
        // одна неподвижная строка, неотличимая от зависания
        auto report = [this, flag](long long done, long long total) {
            brls::sync([this, flag, done, total]() {
                if (!*flag || !statusLabel)
                    return;
                char text[64];
                if (total > 0)
                    std::snprintf(text, sizeof(text), "Загрузка трейлера… %lld%%",
                                  done * 100 / total);
                else
                    std::snprintf(text, sizeof(text), "Загрузка трейлера… %.1f МБ",
                                  static_cast<double>(done) / (1024.0 * 1024.0));
                statusLabel->setText(text);
            });
        };

        // три попытки: обрыв Wi-Fi на середине ролика — обычное дело
        Failure failure = Failure::None;
        std::string path;
        for (int attempt = 0; attempt < 3; attempt++)
        {
            if (attempt > 0)
            {
                // повторяем только обрывы: 4xx и уход с экрана бессмысленны
                if (failure != Failure::Network)
                    break;
                brls::sync([this, flag, attempt]() {
                    if (*flag && statusLabel)
                        statusLabel->setText("Связь оборвалась, попытка "
                                             + std::to_string(attempt + 1) + " из 3…");
                });
                std::this_thread::sleep_for(std::chrono::milliseconds(700 * attempt));
            }
            failure = Failure::None;
            path    = downloadVideo(u, flag.get(), report, &failure);
            if (!path.empty())
                break;
        }

        if (!*flag)
            return;

        brls::sync([this, flag, path, failure]() {
            if (!*flag)
                return;

            if (path.empty())
            {
                statusLabel->setText(
                    failure == Failure::Server
                        ? "Трейлер недоступен на сервере"
                        : (net::isReady() ? "Не удалось загрузить трейлер — связь оборвалась"
                                          : "Не удалось загрузить трейлер — нет сети"));
                return;
            }

            decoder = std::make_unique<VideoDecoder>();
            decoder->start(path);

            statusLabel->setText("Буферизация…");

            surface = new VideoSurface(decoder.get());
            surface->onState = [this](bool loading, bool failed, bool paused) {
                if (failed)
                {
                    statusLabel->setText("Ошибка воспроизведения ролика");
                    statusLabel->setVisibility(brls::Visibility::VISIBLE);
                }
                else if (loading)
                {
                    statusLabel->setText("Буферизация…");
                    statusLabel->setVisibility(brls::Visibility::VISIBLE);
                }
                else
                {
                    statusLabel->setVisibility(brls::Visibility::GONE);
                }

                // пауза — отдельная надпись по центру: иначе непонятно, ролик
                // встал по нажатию или завис
                pauseLabel->setVisibility(paused && !failed ? brls::Visibility::VISIBLE
                                                            : brls::Visibility::GONE);
            };
            videoBox->addView(surface);
        });
    });
}

void VideoPlayerActivity::closeSelf()
{
    brls::Application::popActivity();
}

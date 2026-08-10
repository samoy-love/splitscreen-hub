#include "ui/video_player.hpp"

#include "http_stream.hpp"
#include "net.hpp"
#include "tasks.hpp"

#include <borealis.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <fstream>
#include <memory>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <SDL2/SDL.h>

namespace
{

/// Числовой код FFmpeg сам по себе ничего не сообщает: без расшифровки в логе
/// остаётся вроде «-1094995529», по которому причину не понять.
std::string avErr(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof(buf));
    return std::string(buf) + " (" + std::to_string(code) + ")";
}

/// Есть ли ролик уже в кэше — тогда буферизация не понадобится и надпись
/// должна быть другой.
bool isCached(const std::string& url)
{
    std::ifstream probe(net::cachePath(url, ".mp4"), std::ios::binary | std::ios::ate);
    return probe.good() && probe.tellg() > 0;
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

void VideoDecoder::start(const std::string& url, const std::string& cachePath,
                         std::atomic_bool* alive)
{
    stop();
    running = true;
    worker  = std::thread(&VideoDecoder::decodeLoop, this, url, cachePath, alive);
}

void VideoDecoder::stop()
{
    running = false;
    sleepCv.notify_all();  // будим поток, если он спит между кадрами
    if (worker.joinable())
        worker.join();
}

float VideoDecoder::bufferedFraction() const
{
    // Ролик из кэша читается с диска целиком — буферизовать нечего.
    HttpStream* stream = activeStream.load();
    if (!stream)
        return 1.0f;

    const long long total = stream->total();
    if (total <= 0)
        return 0.0f;
    const float part = static_cast<float>(stream->buffered()) / static_cast<float>(total);
    return part > 1.0f ? 1.0f : part;
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

    // на паузе читатель перестаёт забирать данные, и сетевой поток должен
    // знать об этом, иначе примет затор за обрыв связи
    if (HttpStream* stream = activeStream.load())
        stream->setReaderPaused(paused.load());
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

void VideoDecoder::decodeLoop(std::string url, std::string cachePath,
                              std::atomic_bool* alive)
{
    // Готовый файл в кэше читаем напрямую — это и быстрее, и работает без
    // сети. Иначе открываем сетевой поток, который попутно наполняет кэш.
    std::unique_ptr<HttpStream> stream;

    // Обнуляет указатель на любом пути выхода, включая ранние возвраты по
    // ошибке. Объявлен после stream, поэтому разрушается раньше него —
    // togglePause() из UI-потока не застанет висячий указатель.
    struct StreamGuard
    {
        std::atomic<HttpStream*>* slot;
        ~StreamGuard() { *slot = nullptr; }
    } guard { &activeStream };

    AVFormatContext* fmt = nullptr;

    std::ifstream probe(cachePath, std::ios::binary | std::ios::ate);
    const bool haveFile = probe.good() && probe.tellg() > 0;
    probe.close();

    if (haveFile)
    {
        brls::Logger::info("плеер: играем из кеша {}", cachePath);
        int rc = avformat_open_input(&fmt, cachePath.c_str(), nullptr, nullptr);
        if (rc < 0)
        {
            brls::Logger::error("плеер: кешированный файл не открылся ({}): {}", avErr(rc),
                                cachePath);
            error = true;
            return;
        }
    }
    else
    {
        brls::Logger::info("плеер: потоковое воспроизведение {}", url);
        stream = std::make_unique<HttpStream>(url, cachePath, alive);
        // замерший кадр надо объяснить: без этого обрыв неотличим от
        // зависшего приложения
        activeStream        = stream.get();
        stream->onReconnect = [this](bool active, int attempt) {
            reconnecting = active;
            reconnectTry = attempt;
            if (active)
                brls::Logger::warning("плеер: обрыв, переподключение, попытка {}", attempt);
            else
                brls::Logger::info("плеер: соединение восстановлено");
        };
        fmt    = avformat_alloc_context();
        if (!fmt)
        {
            brls::Logger::error("плеер: не хватило памяти на AVFormatContext");
            error = true;
            return;
        }
        fmt->pb = stream->avio();
        // без имени файла: источник целиком за AVIOContext
        int rc = avformat_open_input(&fmt, nullptr, nullptr, nullptr);
        if (rc < 0)
        {
            brls::Logger::error("плеер: поток не открылся ({}): {}", avErr(rc), url);
            error = true;
            return;
        }
    }
    int rc = avformat_find_stream_info(fmt, nullptr);
    if (rc < 0)
    {
        brls::Logger::error("плеер: не разобрать дорожки ({})", avErr(rc));
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

    brls::Logger::debug("плеер: дорожек {}, видео #{}, звук #{}", fmt->nb_streams, videoIdx,
                        audioIdx);

    // Длительность нужна шкале прогресса. AV_NOPTS_VALUE бывает у потоков без
    // индекса — тогда шкала покажет только прошедшее время.
    if (fmt->duration > 0)
        durationSeconds = static_cast<double>(fmt->duration) / AV_TIME_BASE;

    if (videoIdx < 0)
    {
        brls::Logger::error("плеер: в файле нет видеодорожки");
        avformat_close_input(&fmt);
        error = true;
        return;
    }

    AVCodecParameters* vpar = fmt->streams[videoIdx]->codecpar;
    const AVCodec* vcodec   = avcodec_find_decoder(vpar->codec_id);
    AVCodecContext* vctx    = vcodec ? avcodec_alloc_context3(vcodec) : nullptr;
    if (!vcodec)
        brls::Logger::error("плеер: нет декодера для codec_id {} — возможно, вырезан из сборки",
                            (int)vpar->codec_id);

    if (!vctx || avcodec_parameters_to_context(vctx, vpar) < 0 || avcodec_open2(vctx, vcodec, nullptr) < 0)
    {
        brls::Logger::error("плеер: видеодекодер «{}» не запустился, кадр {}x{}",
                            vcodec ? vcodec->name : "?", vpar->width, vpar->height);
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

                // borealis поднимает SDL без звуковой подсистемы: в
                // sdl_platform.cpp это EVENTS и TIMER, в sdl_video.cpp — VIDEO.
                // Без SDL_INIT_AUDIO SDL_OpenAudioDevice возвращает ноль, и
                // трейлер молча играл беззвучно. Подсистему можно поднять
                // отдельно; повторный вызов безвреден.
                if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
                    brls::Logger::error("плеер: не поднялась звуковая подсистема SDL: {}",
                                        SDL_GetError());

                SDL_AudioSpec want {}, have {};
                want.freq     = actx->sample_rate;
                want.format   = AUDIO_S16SYS;
                want.channels = outChannels;
                want.samples  = 1024;
                dev           = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
                if (dev)
                {
                    brls::Logger::info("плеер: звук {} Гц, каналов {}", have.freq,
                                       (int)have.channels);
                    SDL_PauseAudioDevice(dev, 0);
                }
                else
                {
                    brls::Logger::error("плеер: звуковое устройство не открылось: {}",
                                        SDL_GetError());
                }
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
    {
        SDL_CloseAudioDevice(dev);
        // Подсистему поднимали мы, значит и опускать нам: borealis о ней не
        // знает и при выходе её не тронет.
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
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
        bool recon   = decoder && decoder->isReconnecting();
        int attempt  = decoder ? decoder->reconnectAttempt() : 0;

        // Только на изменение. Раньше колбэк дёргался каждый кадр, а он зовёт
        // Label::setText, и тот помечает узел раскладки грязным — пересчёт
        // шестьдесят раз в секунду всё время буферизации и паузы.
        if (loading != lastLoading || failed != lastFailed || paused != lastPaused
            || recon != lastReconnecting || attempt != lastAttempt)
        {
            lastLoading      = loading;
            lastFailed       = failed;
            lastPaused       = paused;
            lastReconnecting = recon;
            lastAttempt      = attempt;
            onState(loading, failed, paused, recon, attempt);
        }
    }

    if (onProgress && decoder)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastProgress > std::chrono::milliseconds(250))
        {
            lastProgress = now;
            onProgress(decoder->position(), decoder->duration(),
                       decoder->bufferedFraction());
        }
    }

    if (nvgImage < 0)
        return;  // ещё ничего не декодировано — статус-лейбл показывает индикатор поверх

    // Вписываем кадр целиком, сохраняя пропорции: раньше картинка растягивалась
    // на всю выделенную область, и при несовпадении сторон ролик выглядел
    // сплющенным. Поля по краям остаются чёрными — как в обычном плеере.
    const float scale = std::min(width / (float)imageW, height / (float)imageH);
    const float fitW  = imageW * scale;
    const float fitH  = imageH * scale;
    const float fx    = x + (width - fitW) / 2.0f;
    const float fy    = y + (height - fitH) / 2.0f;

    NVGpaint paint = nvgImagePattern(vg, fx, fy, fitW, fitH, 0, nvgImage, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, fx, fy, fitW, fitH);
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
    statusLabel->setText(isCached(url) ? "Открываем трейлер…" : "Буферизация…");
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

namespace
{

/// «3:07» — привычный вид для роликов длиной в минуты.
std::string clock(double seconds)
{
    if (seconds < 0)
        seconds = 0;
    const int total = static_cast<int>(seconds);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
    return buf;
}

}  // namespace

void VideoPlayerActivity::updateProgress(double position, double duration, float buffered)
{
    // Нас зовут четыре раза в секунду, а видимое меняется раз в секунду и на
    // доли процента. setText и setWidthPercentage помечают раскладку грязной,
    // поэтому обновляем только то, что действительно изменилось, — та же
    // ошибка уже была с надписью «Пауза».
    auto setLabel = [](brls::Label* label, std::string& cache, const std::string& text) {
        if (cache == text)
            return;
        cache = text;
        label->setText(text);
    };

    auto setBar = [](brls::Box* bar, float& cache, float percent) {
        if (percent < 0.0f)
            percent = 0.0f;
        if (percent > 100.0f)
            percent = 100.0f;
        // Полшага процента на экране — меньше половины точки ширины.
        if (std::abs(percent - cache) < 0.5f)
            return;
        cache = percent;
        bar->setWidthPercentage(percent);
    };

    setLabel(elapsedLabel, shownElapsed, clock(position));

    if (duration <= 0.0)
    {
        // Длительности нет — показываем только прошедшее время, шкалу прятать
        // не за что: доля закачанного всё равно осмысленна.
        setLabel(leftLabel, shownLeft, "");
        setBar(playedBar, shownPlayed, 0.0f);
    }
    else
    {
        setLabel(leftLabel, shownLeft, "−" + clock(duration - position));
        setBar(playedBar, shownPlayed, 100.0f * static_cast<float>(position / duration));
    }

    setBar(loadedBar, shownLoaded, 100.0f * buffered);
}

void VideoPlayerActivity::beginDownload()
{
    // Ждать полной закачки больше не нужно: декодер сам решает, читать ли
    // готовый файл из кэша или тянуть поток из сети. Показ начинается, как
    // только разобран заголовок, а кэш наполняется попутно.
    decoder = std::make_unique<VideoDecoder>();
    decoder->start(url, net::cachePath(url, ".mp4"), alive.get());

    surface = new VideoSurface(decoder.get());

    // Без явного размера yoga отдаёт поверхности ширину по содержимому, а его у
    // неё нет — кадр рисовался вертикальной полоской посреди чёрного экрана.
    surface->setWidthPercentage(100.0f);
    surface->setHeightPercentage(100.0f);

    // Единственная фокусируемая вещь на экране — сама поверхность. Иначе фокус
    // уходил на невидимую надпись во всю ширину, и стик выделял пустоту.
    // Рамку прячем: подсвечивать видео не нужно, а действия висят на активности.
    surface->setFocusable(true);
    surface->setHideHighlight(true);

    surface->onState = [this](bool loading, bool failed, bool paused,
                              bool reconnecting, int attempt) {
        if (reconnecting)
        {
            statusLabel->setText("Связь оборвалась, продолжаем… ("
                                 + std::to_string(attempt) + " из 5)");
            statusLabel->setVisibility(brls::Visibility::VISIBLE);
        }
        else if (failed)
        {
            statusLabel->setText(net::isReady()
                                     ? "Не удалось воспроизвести трейлер"
                                     : "Не удалось загрузить трейлер — нет сети");
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

        pauseIcon->setVisibility(paused && !failed ? brls::Visibility::VISIBLE
                                                   : brls::Visibility::GONE);
    };

    surface->onProgress = [this](double position, double duration, float buffered) {
        updateProgress(position, duration, buffered);
    };
    videoBox->addView(surface);

    // Явно, а не полагаясь на getDefaultFocus при открытии: поверхность
    // добавляется в конце onContentAvailable, и раньше курсор мог остаться на
    // кнопке трейлера на предыдущем экране.
    brls::Application::giveFocus(surface);
}

void VideoPlayerActivity::closeSelf()
{
    brls::Application::popActivity();
}

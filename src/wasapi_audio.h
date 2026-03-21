#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <thread>
#include <atomic>
#include <functional>
#include <iostream>

// Автоматически линкуем нужную библиотеку Windows для работы с COM-объектами
#pragma comment(lib, "ole32.lib")

// GUID для 32-bit float формата (KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
const GUID MY_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = { 0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71} };

class WasapiAudioDriver {
private:
    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDevice* pDevice = nullptr;
    IAudioClient* pAudioClient = nullptr;
    IAudioRenderClient* pRenderClient = nullptr;
    
    std::thread audioThread;
    std::atomic<bool> isRunning{ false };
    std::function<void(float*, int)> audioCallback;

    void AudioThreadLoop() {
        // Инициализация COM для потока
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        
        UINT32 bufferFrameCount;
        pAudioClient->GetBufferSize(&bufferFrameCount);
        
        // Узнаем, сколько реального времени занимает половина буфера
        int sleepTimeMs = (int)((bufferFrameCount * 1000.0) / 44100.0) / 2;
        if (sleepTimeMs < 1) sleepTimeMs = 1;

        pAudioClient->Start();

        while (isRunning) {
            UINT32 numFramesPadding;
            pAudioClient->GetCurrentPadding(&numFramesPadding);
            
            // Сколько свободного места в буфере звуковой карты?
            UINT32 numFramesAvailable = bufferFrameCount - numFramesPadding;
            
            if (numFramesAvailable > 0) {
                float* pData;
                HRESULT hr = pRenderClient->GetBuffer(numFramesAvailable, (BYTE**)&pData);
                if (SUCCEEDED(hr)) {
                    // Вызываем наш синтезатор!
                    if (audioCallback) {
                        audioCallback(pData, numFramesAvailable);
                    }
                    pRenderClient->ReleaseBuffer(numFramesAvailable, 0);
                }
            }
            // Спим, чтобы не сжечь процессор в бесконечном цикле (ждем пока буфер проиграется)
            Sleep(sleepTimeMs);
        }
        
        pAudioClient->Stop();
        CoUninitialize();
    }

public:
    bool init(std::function<void(float*, int)> callback) {
        audioCallback = callback;
        HRESULT hr;

        CoInitialize(NULL);

        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
        if (FAILED(hr)) return false;

        // Получаем динамики по умолчанию
        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
        if (FAILED(hr)) return false;

        hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);
        if (FAILED(hr)) return false;

        // Настраиваем жесткий формат: 44100 Hz, 2 канала, 32-bit Float
        WAVEFORMATEXTENSIBLE wfx = {};
        wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wfx.Format.nChannels = 2;
        wfx.Format.nSamplesPerSec = 44100;
        wfx.Format.wBitsPerSample = 32;
        wfx.Format.nBlockAlign = (wfx.Format.nChannels * wfx.Format.wBitsPerSample) / 8;
        wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
        wfx.Format.cbSize = 22;
        wfx.Samples.wValidBitsPerSample = 32;
        wfx.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        wfx.SubFormat = MY_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

        // Флаг AUTOCONVERTPCM заставит Windows саму переводить наши 44100Hz в частоту звуковой карты (например 48000Hz)
        DWORD streamFlags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

        // Запрашиваем минимальную задержку (Shared Mode)
        hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, 0, 0, &wfx.Format, NULL);
        if (FAILED(hr)) { std::cerr << "WASAPI Format Error!" << std::endl; return false; }

        hr = pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&pRenderClient);
        if (FAILED(hr)) return false;

        return true;
    }

    void start() {
        if (!isRunning) {
            isRunning = true;
            audioThread = std::thread(&WasapiAudioDriver::AudioThreadLoop, this);
        }
    }

    void stop() {
        if (isRunning) {
            isRunning = false;
            if (audioThread.joinable()) audioThread.join();
        }
    }

    ~WasapiAudioDriver() {
        stop();
        if (pRenderClient) pRenderClient->Release();
        if (pAudioClient) pAudioClient->Release();
        if (pDevice) pDevice->Release();
        if (pEnumerator) pEnumerator->Release();
        CoUninitialize();
    }
};
#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <thread>
#include <atomic>
#include <functional>
#include <iostream>

#pragma comment(lib, "ole32.lib")

// GUID for 32-bit IEEE float PCM (KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
const GUID kSubtypeIeeeFloat = { 0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71} };

class WasapiAudioDriver {
public:
    bool init(std::function<void(float*, int)> callback) {
        audioCallback = callback;

        CoInitialize(NULL);

        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
        if (FAILED(hr)) return false;

        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
        if (FAILED(hr)) return false;

        hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);
        if (FAILED(hr)) return false;

        // Request 44.1kHz stereo 32-bit float
        WAVEFORMATEXTENSIBLE wfx = {};
        wfx.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
        wfx.Format.nChannels       = 2;
        wfx.Format.nSamplesPerSec  = 44100;
        wfx.Format.wBitsPerSample  = 32;
        wfx.Format.nBlockAlign     = (wfx.Format.nChannels * wfx.Format.wBitsPerSample) / 8;
        wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
        wfx.Format.cbSize          = 22;
        wfx.Samples.wValidBitsPerSample = 32;
        wfx.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        wfx.SubFormat     = kSubtypeIeeeFloat;

        // AUTOCONVERTPCM: Windows will resample to the device's native rate (e.g. 48kHz)
        DWORD streamFlags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

        hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, 0, 0, &wfx.Format, NULL);
        if (FAILED(hr)) { std::cerr << "WASAPI format init failed\n"; return false; }

        hr = pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&pRenderClient);
        if (FAILED(hr)) return false;

        return true;
    }

    void start() {
        if (!isRunning) {
            isRunning   = true;
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
        if (pAudioClient)  pAudioClient->Release();
        if (pDevice)       pDevice->Release();
        if (pEnumerator)   pEnumerator->Release();
        CoUninitialize();
    }

private:
    IMMDeviceEnumerator* pEnumerator  = nullptr;
    IMMDevice*           pDevice      = nullptr;
    IAudioClient*        pAudioClient = nullptr;
    IAudioRenderClient*  pRenderClient = nullptr;

    std::thread          audioThread;
    std::atomic<bool>    isRunning{ false };
    std::function<void(float*, int)> audioCallback;

    void AudioThreadLoop() {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);

        UINT32 bufferFrameCount;
        pAudioClient->GetBufferSize(&bufferFrameCount);

        // Sleep half a buffer period to avoid busy-waiting the CPU
        int sleepTimeMs = std::max(1, (int)((bufferFrameCount * 1000.0) / 44100.0) / 2);

        pAudioClient->Start();

        while (isRunning) {
            UINT32 numPadding;
            pAudioClient->GetCurrentPadding(&numPadding);
            UINT32 numAvailable = bufferFrameCount - numPadding;

            if (numAvailable > 0) {
                float* pData;
                HRESULT hr = pRenderClient->GetBuffer(numAvailable, (BYTE**)&pData);
                if (SUCCEEDED(hr)) {
                    if (audioCallback) audioCallback(pData, numAvailable);
                    pRenderClient->ReleaseBuffer(numAvailable, 0);
                }
            }
            Sleep(sleepTimeMs);
        }

        pAudioClient->Stop();
        CoUninitialize();
    }
};

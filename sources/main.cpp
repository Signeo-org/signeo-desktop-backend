#include <iostream>
#include <portaudio.h>

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 512

// Callback function to process incoming audio
static int audioCallback(const void *inputBuffer, void *outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void *userData) {
    const float *in = (const float*)inputBuffer;

    // Log audio data to the terminal
    for (unsigned long i = 0; i < framesPerBuffer; i++) {
        std::cout << in[i] << " ";
    }
    std::cout << std::endl;

    return paContinue;
}

int main() {
    PaError err;

    // Initialize PortAudio
    err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        return -1;
    }

    // Set up input parameters for the audio stream
    PaStreamParameters inputParameters;
    inputParameters.device = Pa_GetDefaultInputDevice();  // Get the default input device
    inputParameters.channelCount = 2;                     // Stereo input
    inputParameters.sampleFormat = paFloat32;             // 32-bit floating point
    inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;

    // Open the audio stream
    PaStream *stream;
    err = Pa_OpenStream(&stream, &inputParameters, nullptr, SAMPLE_RATE,
                        FRAMES_PER_BUFFER, paClipOff, audioCallback, nullptr);
    if (err != paNoError) {
        std::cerr << "Failed to open PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
        Pa_Terminate();
        return -1;
    }

    // Start the stream
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "Failed to start PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(stream);
        Pa_Terminate();
        return -1;
    }

    std::cout << "Capturing audio... Press Enter to stop." << std::endl;
    std::cin.get();  // Wait for user input to stop

    // Stop the stream and terminate PortAudio
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    return 0;
}

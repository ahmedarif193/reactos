/*
 * PROJECT:     ReactOS Sound System Helpers
 * LICENSE:     GPL - See COPYING in the top level directory
 */

#ifndef ROS_AUDIO_WAVEFMT_H
#define ROS_AUDIO_WAVEFMT_H

#include <windef.h>
#include <mmsystem.h>

typedef struct _ROS_SOUND_LEGACY_WAVE_FORMAT
{
    DWORD Flag;
    DWORD SamplesPerSec;
    WORD BitsPerSample;
    WORD Channels;
} ROS_SOUND_LEGACY_WAVE_FORMAT, *PROS_SOUND_LEGACY_WAVE_FORMAT;

static const ROS_SOUND_LEGACY_WAVE_FORMAT RosSoundLegacyWaveFormats[] =
{
    { WAVE_FORMAT_1M08,  11025,  8, 1 },
    { WAVE_FORMAT_1M16,  11025, 16, 1 },
    { WAVE_FORMAT_1S08,  11025,  8, 2 },
    { WAVE_FORMAT_1S16,  11025, 16, 2 },
    { WAVE_FORMAT_2M08,  22050,  8, 1 },
    { WAVE_FORMAT_2M16,  22050, 16, 1 },
    { WAVE_FORMAT_2S08,  22050,  8, 2 },
    { WAVE_FORMAT_2S16,  22050, 16, 2 },
    { WAVE_FORMAT_4M08,  44100,  8, 1 },
    { WAVE_FORMAT_4M16,  44100, 16, 1 },
    { WAVE_FORMAT_4S08,  44100,  8, 2 },
    { WAVE_FORMAT_4S16,  44100, 16, 2 },
    { WAVE_FORMAT_48M08, 48000,  8, 1 },
    { WAVE_FORMAT_48M16, 48000, 16, 1 },
    { WAVE_FORMAT_48S08, 48000,  8, 2 },
    { WAVE_FORMAT_48S16, 48000, 16, 2 },
    { WAVE_FORMAT_96M08, 96000,  8, 1 },
    { WAVE_FORMAT_96M16, 96000, 16, 1 },
    { WAVE_FORMAT_96S08, 96000,  8, 2 },
    { WAVE_FORMAT_96S16, 96000, 16, 2 },
    { 0, 0, 0, 0 }
};

static inline DWORD
RosSoundWaveFormatFieldsToLegacyFlag(
    _In_ DWORD SamplesPerSec,
    _In_ WORD BitsPerSample,
    _In_ WORD Channels)
{
    const ROS_SOUND_LEGACY_WAVE_FORMAT *Format;

    for (Format = RosSoundLegacyWaveFormats; Format->Flag; Format++)
    {
        if (Format->SamplesPerSec == SamplesPerSec &&
            Format->BitsPerSample == BitsPerSample &&
            Format->Channels == Channels)
        {
            return Format->Flag;
        }
    }

    return 0;
}

static inline BOOL
RosSoundLegacyFlagToWaveFormatFields(
    _In_ DWORD Flag,
    _Out_ DWORD *SamplesPerSec,
    _Out_ WORD *BitsPerSample,
    _Out_ WORD *Channels)
{
    const ROS_SOUND_LEGACY_WAVE_FORMAT *Format;

    for (Format = RosSoundLegacyWaveFormats; Format->Flag; Format++)
    {
        if (Format->Flag == Flag)
        {
            *SamplesPerSec = Format->SamplesPerSec;
            *BitsPerSample = Format->BitsPerSample;
            *Channels = Format->Channels;
            return TRUE;
        }
    }

    return FALSE;
}

static inline DWORD
RosSoundWaveFormatToLegacyFlag(
    _In_ const WAVEFORMATEX *WaveFormat)
{
    if (!WaveFormat || WaveFormat->wFormatTag != WAVE_FORMAT_PCM)
        return 0;

    return RosSoundWaveFormatFieldsToLegacyFlag(WaveFormat->nSamplesPerSec,
                                                WaveFormat->wBitsPerSample,
                                                WaveFormat->nChannels);
}

static inline DWORD
RosSoundAudioRangeToLegacyFlags(
    _In_ DWORD MinimumSampleFrequency,
    _In_ DWORD MaximumSampleFrequency,
    _In_ WORD MinimumBitsPerSample,
    _In_ WORD MaximumBitsPerSample,
    _In_ WORD MaximumChannels)
{
    const ROS_SOUND_LEGACY_WAVE_FORMAT *Format;
    DWORD Flags = 0;

    for (Format = RosSoundLegacyWaveFormats; Format->Flag; Format++)
    {
        if (MinimumSampleFrequency <= Format->SamplesPerSec &&
            MaximumSampleFrequency >= Format->SamplesPerSec &&
            MinimumBitsPerSample <= Format->BitsPerSample &&
            MaximumBitsPerSample >= Format->BitsPerSample &&
            MaximumChannels >= Format->Channels)
        {
            Flags |= Format->Flag;
        }
    }

    return Flags;
}

static inline DWORD
RosSoundChooseBestLegacyFormat(
    _In_ DWORD Flags)
{
    static const DWORD PreferredFormats[] =
    {
        WAVE_FORMAT_48S16,
        WAVE_FORMAT_48M16,
        WAVE_FORMAT_4S16,
        WAVE_FORMAT_4M16,
        WAVE_FORMAT_96S16,
        WAVE_FORMAT_96M16,
        WAVE_FORMAT_2S16,
        WAVE_FORMAT_2M16,
        WAVE_FORMAT_1S16,
        WAVE_FORMAT_1M16,
        WAVE_FORMAT_48S08,
        WAVE_FORMAT_48M08,
        WAVE_FORMAT_4S08,
        WAVE_FORMAT_4M08,
        WAVE_FORMAT_96S08,
        WAVE_FORMAT_96M08,
        WAVE_FORMAT_2S08,
        WAVE_FORMAT_2M08,
        WAVE_FORMAT_1S08,
        WAVE_FORMAT_1M08
    };
    ULONG Index;

    for (Index = 0; Index < sizeof(PreferredFormats) / sizeof(PreferredFormats[0]); Index++)
    {
        if (Flags & PreferredFormats[Index])
            return PreferredFormats[Index];
    }

    return 0;
}

#endif /* ROS_AUDIO_WAVEFMT_H */

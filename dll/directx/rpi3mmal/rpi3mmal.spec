# PROJECT:     ReactOS Raspberry Pi 3 video support
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Private user-mode VideoCore MMAL codec exports
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

@ stdcall Rpi3MmalQueryCaps(ptr)
@ stdcall Rpi3MmalCreateH264Decoder(long long ptr long ptr)
@ stdcall Rpi3MmalSubmit(ptr ptr long long int64 int64)
@ stdcall Rpi3MmalReceiveNV12(ptr ptr long long long ptr)
@ stdcall Rpi3MmalReceiveNV12Selected(ptr long ptr ptr ptr)
@ stdcall Rpi3MmalFlush(ptr)
@ stdcall Rpi3MmalDestroyDecoder(ptr)
@ stdcall Rpi3MmalCreateH264Encoder(ptr ptr)
@ stdcall Rpi3MmalSubmitNV12(ptr ptr long long long int64 int64)
@ stdcall Rpi3MmalReceiveH264(ptr ptr long long ptr)
@ stdcall Rpi3MmalRequestKeyFrame(ptr)
@ stdcall Rpi3MmalSetBitrate(ptr long)
@ stdcall Rpi3MmalSetIntraPeriod(ptr long)
@ stdcall Rpi3MmalFlushEncoder(ptr)
@ stdcall Rpi3MmalDestroyEncoder(ptr)

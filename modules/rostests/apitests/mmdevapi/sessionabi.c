/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Compile-time parity between the tests and mmdevapi's private layout
 */

#define COBJMACROS

#include <windows.h>
#include <objbase.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <mmdevapi_private.h>

C_ASSERT(REACTOS_AUDIO_SESSION_MAX_CHANNELS == 32);
C_ASSERT(sizeof(struct reactos_audio_session_id) == 20);
C_ASSERT(sizeof(struct reactos_audio_session_snapshot) == 1480);
C_ASSERT(FIELD_OFFSET(struct reactos_audio_session_id, instance_guid) == 4);
C_ASSERT(FIELD_OFFSET(struct reactos_audio_session_snapshot, process_id) == 32);
C_ASSERT(FIELD_OFFSET(struct reactos_audio_session_snapshot, generation) == 44);
C_ASSERT(FIELD_OFFSET(struct reactos_audio_session_snapshot, master_volume) == 48);
C_ASSERT(FIELD_OFFSET(struct reactos_audio_session_snapshot, mute) == 52);
C_ASSERT(FIELD_OFFSET(struct reactos_audio_session_snapshot, channel_volumes) == 56);
C_ASSERT(FIELD_OFFSET(struct reactos_audio_session_snapshot, process_path) == 184);
C_ASSERT(FIELD_OFFSET(struct reactos_audio_session_snapshot, display_name) == 704);
C_ASSERT(FIELD_OFFSET(struct reactos_audio_session_snapshot, icon_path) == 960);

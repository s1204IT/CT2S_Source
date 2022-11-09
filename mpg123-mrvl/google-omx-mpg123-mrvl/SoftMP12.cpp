/*
 * (C) Copyright 2014 Marvell International Ltd.
 * All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "SoftMP12"
#include <utils/Log.h>

#include "SoftMP12.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include "mpg123.h"

namespace android {
static Mutex gMPG123Mutex;
static int gInstanceNum=0;
template<class T>
static void InitOMXParams(T *params) {
    params->nSize = sizeof(T);
    params->nVersion.s.nVersionMajor = 1;
    params->nVersion.s.nVersionMinor = 0;
    params->nVersion.s.nRevision = 0;
    params->nVersion.s.nStep = 0;
}

SoftMP12::SoftMP12(
        const char *name,
        const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData,
        OMX_COMPONENTTYPE **component)
    : SimpleSoftOMXComponent(name, callbacks, appData, component),
      mConfig(NULL),
      mAnchorTimeUs(-1),
      mCurrentTimeUs(-1),
      mNumChannels(2),
      mSamplingRate(44100),
      mSignalledError(false),
      mOutputPortSettingsChange(NONE) {
    initPorts();
    initDecoder();
}

SoftMP12::~SoftMP12() {
    deinitDecoder();
}

void SoftMP12::initPorts() {
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);

    def.nPortIndex = 0;
    def.eDir = OMX_DirInput;
    def.nBufferCountMin = kNumBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = 8192;
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainAudio;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 1;

    def.format.audio.cMIMEType =
        const_cast<char *>(MEDIA_MIMETYPE_AUDIO_MPEG);

    def.format.audio.pNativeRender = NULL;
    def.format.audio.bFlagErrorConcealment = OMX_FALSE;
    def.format.audio.eEncoding = OMX_AUDIO_CodingMP3;

    addPort(def);

    def.nPortIndex = 1;
    def.eDir = OMX_DirOutput;
    def.nBufferCountMin = kNumBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = kOutputBufferSize;
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainAudio;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 2;

    def.format.audio.cMIMEType = const_cast<char *>("audio/raw");
    def.format.audio.pNativeRender = NULL;
    def.format.audio.bFlagErrorConcealment = OMX_FALSE;
    def.format.audio.eEncoding = OMX_AUDIO_CodingPCM;

    addPort(def);
}

void SoftMP12::initDecoder() {
    if (mConfig == NULL) {
	    int ret;
        Mutex::Autolock autoLock(gMPG123Mutex);
        if (gInstanceNum == 0)  {
            mpg123_init();
        }
        gInstanceNum++;
        mConfig = mpg123_new(NULL, &ret);
        if(mConfig == NULL) {
            return;
        }
        ret = mpg123_open_feed(mConfig);
        if(ret != MPG123_OK) {
            return;
        }
    }
    mIsFirst = true;
    mAnchorTimeUs = -1;
    mCurrentTimeUs = -1;
}
void SoftMP12::deinitDecoder() {
    if (mConfig != NULL) {
        mpg123_delete(mConfig);
        Mutex::Autolock autoLock(gMPG123Mutex);
        gInstanceNum--;
        if (gInstanceNum == 0)  {
            mpg123_exit();
        }
        mConfig = NULL;
    } 
}
OMX_ERRORTYPE SoftMP12::internalGetParameter(
        OMX_INDEXTYPE index, OMX_PTR params) {
    switch (index) {
        case OMX_IndexParamAudioPcm:
        {
            OMX_AUDIO_PARAM_PCMMODETYPE *pcmParams =
                (OMX_AUDIO_PARAM_PCMMODETYPE *)params;

            if (pcmParams->nPortIndex > 1) {
                return OMX_ErrorUndefined;
            }

            pcmParams->eNumData = OMX_NumericalDataSigned;
            pcmParams->eEndian = OMX_EndianBig;
            pcmParams->bInterleaved = OMX_TRUE;
            pcmParams->nBitPerSample = 16;
            pcmParams->ePCMMode = OMX_AUDIO_PCMModeLinear;
            pcmParams->eChannelMapping[0] = OMX_AUDIO_ChannelLF;
            pcmParams->eChannelMapping[1] = OMX_AUDIO_ChannelRF;

            pcmParams->nChannels = mNumChannels;
            pcmParams->nSamplingRate = mSamplingRate;

            return OMX_ErrorNone;
        }
        case OMX_IndexParamAudioMp3:
        {
            OMX_AUDIO_PARAM_MP3TYPE *mp3Params =
                (OMX_AUDIO_PARAM_MP3TYPE *)params;

            if (mp3Params->nPortIndex > 1) {
                return OMX_ErrorUndefined;
            }

            mp3Params->nChannels = mNumChannels;
            mp3Params->nBitRate = 0 /* unknown */;
            mp3Params->nSampleRate = mSamplingRate;
            // other fields are encoder-only

            return OMX_ErrorNone;
        }

        default:
            return SimpleSoftOMXComponent::internalGetParameter(index, params);
    }
}

OMX_ERRORTYPE SoftMP12::internalSetParameter(
        OMX_INDEXTYPE index, const OMX_PTR params) {
    switch (index) {
        case OMX_IndexParamStandardComponentRole:
        {
            const OMX_PARAM_COMPONENTROLETYPE *roleParams =
                (const OMX_PARAM_COMPONENTROLETYPE *)params;

            if (strncmp((const char *)roleParams->cRole,
                        "audio_decoder.mp1",
                        OMX_MAX_STRINGNAME_SIZE - 1) && 
                strncmp((const char *)roleParams->cRole,
                        "audio_decoder.mp2",
                        OMX_MAX_STRINGNAME_SIZE - 1)) {
                return OMX_ErrorUndefined;
            }

            return OMX_ErrorNone;
        }

        default:
            return SimpleSoftOMXComponent::internalSetParameter(index, params);
    }
}

void SoftMP12::onQueueFilled(OMX_U32 portIndex) {
    if (mSignalledError || mOutputPortSettingsChange != NONE) {
        return;
    }

    List<BufferInfo *> &inQueue = getPortQueue(0);
    List<BufferInfo *> &outQueue = getPortQueue(1);
    bool bEOS=true;
    while (!inQueue.empty() && !outQueue.empty()) {
        BufferInfo *inInfo = *inQueue.begin();
        OMX_BUFFERHEADERTYPE *inHeader = inInfo->mHeader;

        BufferInfo *outInfo = *outQueue.begin();
        OMX_BUFFERHEADERTYPE *outHeader = outInfo->mHeader;

        if (mAnchorTimeUs == -1 && !(inHeader->nFlags & OMX_BUFFERFLAG_CODECCONFIG)) {
            mAnchorTimeUs = inHeader->nTimeStamp;
            mCurrentTimeUs = mAnchorTimeUs;
            mLastInputTimeUs = mCurrentTimeUs;
        }
        if (inHeader->nFilledLen && (inHeader->nTimeStamp > mLastInputTimeUs + 500000LL || inHeader->nTimeStamp < mLastInputTimeUs - 80000LL)) {
            mCurrentTimeUs = inHeader->nTimeStamp;
        }
        mLastInputTimeUs = inHeader->nTimeStamp;
		int ret=MPG123_OK;
        if (inHeader->nFilledLen) {
            ret = mpg123_feed(mConfig, inHeader->pBuffer + inHeader->nOffset, inHeader->nFilledLen);
            inHeader->nOffset = 0;
            inHeader->nFilledLen = 0;
        }
        if (!(inHeader->nFlags & OMX_BUFFERFLAG_EOS)) {
            //will hold the last EOS buffer until there is no output.
            inInfo->mOwnedByUs = false;
            inQueue.erase(inQueue.begin());
            inInfo = NULL;
            notifyEmptyBufferDone(inHeader);
            inHeader = NULL;
            bEOS = false;
        }

        if (ret == MPG123_OK) {
            uint8_t *audio=NULL;
            size_t bytes;
            off_t num;
            ret = mpg123_decode_frame(mConfig, &num, &audio, &bytes);
            if(ret == MPG123_NEW_FORMAT) {
                long int rate;
                int32_t channels, enc;
                mpg123_getformat(mConfig, &rate, &channels, &enc);
                if (rate != mSamplingRate || channels != mNumChannels) {
                    mSamplingRate = rate;
                    mNumChannels = channels;
                    notify(OMX_EventPortSettingsChanged, 1, 0, NULL);
                    mOutputPortSettingsChange = AWAITING_DISABLED;
                    return;
                }
            } else if (ret == MPG123_NEED_MORE) {
                if (inHeader != NULL && (inHeader->nFlags & OMX_BUFFERFLAG_EOS)) {
                    //there is no more output data.
                    //release input and output buffers.
                    inQueue.erase(inQueue.begin());
                    inInfo->mOwnedByUs = false;
                    notifyEmptyBufferDone(inHeader);
                    outHeader->nFlags = OMX_BUFFERFLAG_EOS;

                    outQueue.erase(outQueue.begin());
                    outInfo->mOwnedByUs = false;
                    notifyFillBufferDone(outHeader);
                    return;
                } else {
                    continue;
                }
            }
            if (audio) {
                memcpy(outHeader->pBuffer, audio, bytes);
                outHeader->nFilledLen = bytes;
            }
        } else {
            continue;
        }
        if (mIsFirst && outHeader->nFilledLen != 0) {
            mIsFirst = false;
        }

        outHeader->nTimeStamp = mCurrentTimeUs;
        mCurrentTimeUs += (((outHeader->nFilledLen >> 1)/mNumChannels) * 1000000ll) / mSamplingRate;

        outHeader->nFlags = 0;  

        outInfo->mOwnedByUs = false;
        outQueue.erase(outQueue.begin());
        outInfo = NULL;
        notifyFillBufferDone(outHeader);
        outHeader = NULL;
    }
}

void SoftMP12::onPortFlushCompleted(OMX_U32 portIndex) {
    if (portIndex == 0) {
        deinitDecoder();
        initDecoder();
    }
}

void SoftMP12::onPortEnableCompleted(OMX_U32 portIndex, bool enabled) {
    if (portIndex != 1) {
        return;
    }

    switch (mOutputPortSettingsChange) {
        case NONE:
            break;

        case AWAITING_DISABLED:
        {
            CHECK(!enabled);
            mOutputPortSettingsChange = AWAITING_ENABLED;
            break;
        }

        default:
        {
            CHECK_EQ((int)mOutputPortSettingsChange, (int)AWAITING_ENABLED);
            CHECK(enabled);
            mOutputPortSettingsChange = NONE;
            break;
        }
    }
}

}  // namespace android

android::SoftOMXComponent *createSoftOMXComponent(
        const char *name, const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData, OMX_COMPONENTTYPE **component) {
    return new android::SoftMP12(name, callbacks, appData, component);
}

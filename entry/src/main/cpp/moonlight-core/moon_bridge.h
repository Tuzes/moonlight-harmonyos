//
// Created on 2023/12/6.
//
// Node APIs are not fully supported. To solve the compilation error of the interface cannot be found,
// please include "napi/native_api.h".

#ifndef moonlight_moon_bridge_H
#define moonlight_moon_bridge_H
#include "node_api.h"
#include "video/render/plugin_render.h"
#include "audio/Audio.h"
#include "audio/SDLAudioRenderer.h"
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <string>
#include <unordered_map>
#include <video/VideoRender.h>

char *get_value_string(napi_env env, napi_value value);
int MoonBridge_sendTouchEvent(OH_NativeXComponent_TouchEvent touchEvent, uint64_t width, uint64_t height);
int MoonBridge_sendMouseEvent(OH_NativeXComponent_MouseEvent mouseEvent, uint64_t width, uint64_t height);

struct MoonBridgeCallBackInfo{
    char* stage;
    int error;
};


class MoonBridgeApi {
  public:
    explicit MoonBridgeApi();
    ~MoonBridgeApi();

    static MoonBridgeApi *api;
    
    IVideoDecoder *m_decoder;
    VideoRender *m_render;
    EglVideoRenderer *m_videoRender;
    SDLAudioRenderer *m_audioRender;
    void *nativewindow;
    void Export(napi_env env, napi_value exports);
    napi_threadsafe_function getFunByName(char *name);
    int setFunByName(char *name, napi_threadsafe_function tsf);
  private:
    napi_env env;
    static napi_value Emit(char *eventName, void* value);
    static napi_value On(napi_env env, napi_callback_info info);
    std::unordered_map<std::string, napi_threadsafe_function> m_funRefs;
    DECODER_RENDERER_CALLBACKS BridgeVideoRendererCallbacks;
    AUDIO_RENDERER_CALLBACKS BridgeAudioRendererCallbacks;
    CONNECTION_LISTENER_CALLBACKS BridgeConnListenerCallbacks;
    static POPUS_MULTISTREAM_CONFIGURATION config;
   
    static napi_value MoonBridge_startConnection(napi_env env, napi_callback_info info);
    static napi_value MoonBridge_stopConnection(napi_env env, napi_callback_info info);
    static napi_value MoonBridge_interruptConnection(napi_env env, napi_callback_info info);

    static int BridgeDrSetup(int videoFormat, int width, int height, int redrawRate, void *context, int drFlags) {
        DECODER_PARAMETERS param;
        param.video_format = videoFormat;
        param.width = width;
        param.height = height;
        param.context = context;
        param.frame_rate = redrawRate;
        param.dr_flags = drFlags;
        if (api->m_decoder != nullptr){
            api->m_decoder->setup(param);
        }
        return DR_OK;
    }
    static void BridgeDrStart(void) {
        api->m_decoder->start();
    }
    static void BridgeDrStop(void) {
        api->m_decoder->stop();
    }
    static void BridgeDrCleanup(void) {
        try {
            //api->m_decoder->cleanup();
        } catch (...) {
             // 捕获所有其他类型的异常
             OH_LOG_Print(LOG_APP, LOG_ERROR, LOG_DOMAIN, "testTag", "m_decoder.cleanup() failure");
         }
    }
    static int BridgeDrSubmitDecodeUnit(PDECODE_UNIT decodeUnit) {
        int ret = api->m_decoder->submitDecodeUnit(decodeUnit);
        //Emit("OnVideoStatus", api->m_decoder->video_decode_stats());
        return ret;
    }

    static int BridgeArInit(int audioConfiguration, POPUS_MULTISTREAM_CONFIGURATION opusConfig, void *context, int flags) {
        return api->m_audioRender->init(audioConfiguration, opusConfig, context, flags);
    }
    static void BridgeArStart(void) {
        api->m_audioRender->start();
    }
    static void BridgeArStop(void) {
        api->m_audioRender->stop();
    }
    static void BridgeArCleanup() {
        api->m_audioRender->cleanup();
    }
    static void BridgeArDecodeAndPlaySample(char *sampleData, int sampleLength) {
        api->m_audioRender->decode_and_play_sample(sampleData, sampleLength);
    }

    static void BridgeClStageStarting(int stage) {
        const char *name = LiGetStageName(stage);
        MoonBridgeCallBackInfo info = {
            .stage = (char* )name
        };
        Emit("BridgeClStageStarting", static_cast<void*>(&info));
    }
    static void BridgeClStageComplete(int stage) {
        const char *name = LiGetStageName(stage);
        MoonBridgeCallBackInfo info = {
            .stage = (char* )name
        };
        Emit("BridgeClStageComplete", static_cast<void*>(&info));
    }
    static void BridgeClStageFailed(int stage, int errorCode) {
        napi_value params[2];
        const char *name = LiGetStageName(stage);
         MoonBridgeCallBackInfo info = {
            .stage = (char* )name,
            .error = errorCode
        };
        Emit("BridgeClStageFailed", static_cast<void*>(&info));
    }
    static void BridgeClConnectionStarted(void) {
        napi_value params[0];
        MoonBridgeCallBackInfo info = {
            .stage = "",
            .error = 1
        };
        Emit("BridgeClConnectionStarted", static_cast<void*>(&info));
    }
    static void BridgeClConnectionTerminated(int errorCode) {
        napi_value params[1];
        napi_create_int32(api->env, errorCode, &params[0]);
         MoonBridgeCallBackInfo info = {
            .stage = "xxxxxxx",
            .error = -1
        };
        Emit("BridgeClConnectionTerminated", static_cast<void*>(&info));
    }
    static void BridgeClConnectionStatusUpdate(int connectionStatus) {
        napi_value params[1];
        napi_create_int32(api->env, connectionStatus, &params[0]);
         MoonBridgeCallBackInfo info = {
            .stage = "xxx",
            .error = 1
        };
        Emit("BridgeClConnectionTerminated", static_cast<void*>(&info));
    }

    static void BridgeClSetHdrMode(bool enabled) {
        napi_value params[1];
        napi_create_int32(api->env, enabled, &params[0]);
        Emit("BridgeClSetHdrMode", params);
    }
    static void BridgeClRumble(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor) {
        napi_value params[3];
    }
    static void BridgeClRumbleTriggers(unsigned short controllerNumber, unsigned short leftTrigger, unsigned short rightTrigger) {
        napi_value params[3];
        // napi_create_int32(MoonBridge::env, controllerNumber, &params[0]);
        // napi_create_int32(MoonBridge::env, leftTrigger, &params[1]);
        // napi_create_int32(MoonBridge::env, rightTrigger, &params[2]);
       // Emit("BridgeClRumbleTriggers", params);
    }
    static void BridgeClSetMotionEventState(uint16_t controllerNumber, uint8_t motionType, uint16_t reportRateHz) {
        napi_value params[1];
    }
    static void BridgeClSetControllerLED(uint16_t controllerNumber, uint8_t r, uint8_t g, uint8_t b) {
        napi_value params[4];
        // MoonBridge::Emit("BridgeClSetControllerLED", params);
    }
    static void BridgeClLogMessage(const char *format, ...) {
        va_list va;
        va_start(va, format);
        OH_LOG_Print(LOG_APP, LOG_INFO, LOG_DOMAIN, "testTag", format, va);
        va_end(va);
    }
};

#endif // moonlight_moon_bridge_javascript_class_H

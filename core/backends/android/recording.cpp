#include "android_backend.h"
#include <android_native_app_glue.h>

namespace backend {
    extern android_app* app;

    class ActivityCall {
    public:
        ActivityCall() {
            if (!app || !app->activity) { return; }
            vm = app->activity->vm;
            if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
                attached = vm->AttachCurrentThread(&env, nullptr) == JNI_OK;
                if (!attached) { env = nullptr; }
            }
            if (env) { activityClass = env->GetObjectClass(app->activity->clazz); }
        }

        ~ActivityCall() {
            if (env) {
                if (env->ExceptionCheck()) { env->ExceptionClear(); }
                if (activityClass) { env->DeleteLocalRef(activityClass); }
            }
            if (attached) { vm->DetachCurrentThread(); }
        }

        std::string text(const char* method) {
            if (!env || !activityClass) { return "Android activity unavailable"; }
            auto methodId = env->GetMethodID(activityClass, method, "()Ljava/lang/String;");
            if (!methodId) { return "Android method unavailable"; }
            auto result = static_cast<jstring>(env->CallObjectMethod(app->activity->clazz, methodId));
            if (!result || env->ExceptionCheck()) { return "Android call failed"; }
            const char* chars = env->GetStringUTFChars(result, nullptr);
            std::string value = chars ? chars : "";
            if (chars) { env->ReleaseStringUTFChars(result, chars); }
            env->DeleteLocalRef(result);
            return value;
        }

        JNIEnv* env = nullptr;
        jclass activityClass = nullptr;
    private:
        JavaVM* vm = nullptr;
        bool attached = false;
    };

    void setSessionLocked(bool locked) {
        ActivityCall call;
        if (!call.env || !call.activityClass) { return; }
        auto method = call.env->GetMethodID(call.activityClass, "setSessionLocked", "(Z)V");
        if (method) { call.env->CallVoidMethod(app->activity->clazz, method, static_cast<jboolean>(locked)); }
    }

    void setScreenOrientation(int orientation) {
        ActivityCall call;
        if (!call.env || !call.activityClass) { return; }
        auto method = call.env->GetMethodID(call.activityClass, "setScreenOrientation", "(I)V");
        if (method) { call.env->CallVoidMethod(app->activity->clazz, method, static_cast<jint>(orientation)); }
    }

    void chooseRecordingDirectory() {
        ActivityCall call;
        if (!call.env || !call.activityClass) { return; }
        auto method = call.env->GetMethodID(call.activityClass, "chooseRecordingDirectory", "()V");
        if (method) { call.env->CallVoidMethod(app->activity->clazz, method); }
    }

    std::string recordingDirectoryResult() { return ActivityCall().text("getRecordingDirectoryResult"); }
    std::string recordingError() { return ActivityCall().text("getRecordingError"); }

    bool canWriteRecordingDirectory(const std::string& uri) {
        ActivityCall call;
        if (!call.env || !call.activityClass) { return false; }
        auto method = call.env->GetMethodID(call.activityClass, "canWriteRecordingDirectory", "(Ljava/lang/String;)Z");
        if (!method) { return false; }
        auto value = call.env->NewStringUTF(uri.c_str());
        bool writable = call.env->CallBooleanMethod(app->activity->clazz, method, value);
        call.env->DeleteLocalRef(value);
        return !call.env->ExceptionCheck() && writable;
    }

    int openRecordingDocument(const std::string& tree, const std::string& filename) {
        ActivityCall call;
        if (!call.env || !call.activityClass) { return -1; }
        auto method = call.env->GetMethodID(call.activityClass, "openRecordingDocument", "(Ljava/lang/String;Ljava/lang/String;)I");
        if (!method) { return -1; }
        auto treeValue = call.env->NewStringUTF(tree.c_str());
        auto nameValue = call.env->NewStringUTF(filename.c_str());
        int descriptor = call.env->CallIntMethod(app->activity->clazz, method, treeValue, nameValue);
        call.env->DeleteLocalRef(treeValue);
        call.env->DeleteLocalRef(nameValue);
        return call.env->ExceptionCheck() ? -1 : descriptor;
    }
}

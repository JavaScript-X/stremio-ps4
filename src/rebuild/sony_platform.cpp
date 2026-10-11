#include "sony_platform.h"

#include <orbis/CommonDialog.h>
#include <orbis/Http.h>
#include <orbis/ImeDialog.h>
#include <orbis/Net.h>
#include <orbis/Pad.h>
#include <orbis/Ssl.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

extern "C" int sceSysUtilSendSystemNotificationWithText(int, const char*);

namespace rebuild {
namespace {
constexpr uint32_t timeout = 8000000;
}

bool SonyPlatform::start() {
    OrbisUserServiceInitializeParams params{};
    params.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    sceUserServiceInitialize(&params);
    if (sceUserServiceGetInitialUser(&userId_) != 0 || scePadInit() != 0)
        return false;
    pad_ = scePadOpen(userId_, 0, 0, nullptr);
    return pad_ >= 0;
}

void SonyPlatform::stop() {
    if (pad_ >= 0) scePadClose(pad_);
    if (http_ > 0) sceHttpTerm(http_);
    if (ssl_ > 0) sceSslTerm();
    if (netPool_ > 0) sceNetPoolDestroy(netPool_);
    pad_ = -1; http_ = ssl_ = netPool_ = 0;
}

uint32_t SonyPlatform::buttons() {
    OrbisPadData data{};
    if (pad_ < 0 || scePadReadState(pad_, &data) != 0) return 0;
    uint32_t result = data.buttons;
    if (data.leftStick.x < 58) result |= ORBIS_PAD_BUTTON_LEFT;
    if (data.leftStick.x > 198) result |= ORBIS_PAD_BUTTON_RIGHT;
    if (data.leftStick.y < 58) result |= ORBIS_PAD_BUTTON_UP;
    if (data.leftStick.y > 198) result |= ORBIS_PAD_BUTTON_DOWN;
    return result;
}

void SonyPlatform::notify(const char* message) const {
    sceSysUtilSendSystemNotificationWithText(222, message);
}

bool SonyPlatform::online() {
    if (http_ > 0) return true;
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET) < 0 ||
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP) < 0 ||
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL) < 0)
        return false;
    sceNetInit();
    netPool_ = sceNetPoolCreate("stremio-rebuild", 64 * 1024, 0);
    ssl_ = netPool_ > 0 ? sceSslInit(304 * 1024) : -1;
    http_ = ssl_ > 0 ? sceHttpInit(netPool_, ssl_, 512 * 1024) : -1;
    return http_ > 0;
}

int SonyPlatform::get(const std::string& url, size_t limit, std::string& body) {
    body.clear();
    if (!online()) return -1;
    const int tmpl = sceHttpCreateTemplate(http_, "StremioPS4-Rebuild/1.0",
        ORBIS_HTTP_VERSION_1_1, 1);
    if (tmpl < 0) return -2;
    sceHttpSetResolveTimeOut(tmpl, timeout);
    sceHttpSetConnectTimeOut(tmpl, timeout);
    sceHttpSetSendTimeOut(tmpl, timeout);
    const int conn = sceHttpCreateConnectionWithURL(tmpl, url.c_str(), false);
    const int req = conn >= 0 ? sceHttpCreateRequestWithURL(
        conn, ORBIS_METHOD_GET, url.c_str(), 0) : -1;
    int result = -3;
    if (req >= 0 && sceHttpSendRequest(req, nullptr, 0) >= 0) {
        int status = 0;
        if (sceHttpGetStatusCode(req, &status) >= 0 && status == 200) {
            char chunk[8192]; result = 0;
            for (;;) {
                const int n = sceHttpReadData(req, chunk, sizeof(chunk));
                if (n < 0) { result = -4; break; }
                if (!n) { result = static_cast<int>(body.size()); break; }
                if (body.size() + static_cast<size_t>(n) > limit) {
                    result = -5; break;
                }
                body.append(chunk, static_cast<size_t>(n));
            }
        } else result = status > 0 ? -status : -6;
    }
    if (req >= 0) sceHttpDeleteRequest(req);
    if (conn >= 0) sceHttpDeleteConnection(conn);
    sceHttpDeleteTemplate(tmpl);
    return result;
}

bool SonyPlatform::keyboard(const char* title, std::string& value) {
    if (!dialogs_) {
        if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG) < 0 ||
            sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG) < 0) return false;
        sceCommonDialogInitialize(); dialogs_ = true;
    }
    uint16_t buffer[65]{};
    for (size_t i = 0; i < value.size() && i < 64; ++i)
        buffer[i] = static_cast<uint8_t>(value[i]);
    uint16_t heading[65]{};
    for (size_t i = 0; title[i] && i < 64; ++i)
        heading[i] = static_cast<uint8_t>(title[i]);
    OrbisImeDialogSetting settings{};
    settings.userId = static_cast<uint32_t>(userId_);
    settings.type = ORBIS_TYPE_DEFAULT;
    settings.enterLabel = ORBIS_BUTTON_LABEL_SEARCH;
    settings.inputMethod = ORBIS__DEFAULT;
    settings.maxTextLength = 64;
    settings.inputTextBuffer = reinterpret_cast<wchar_t*>(buffer);
    settings.title = reinterpret_cast<const wchar_t*>(heading);
    settings.posx = 960; settings.posy = 540;
    settings.horizontalAlignment = ORBIS_H_CENTER;
    settings.verticalAlignment = ORBIS_V_CENTER;
    if (sceImeDialogInit(&settings, nullptr) < 0) return false;
    while (sceImeDialogGetStatus() == ORBIS_DIALOG_STATUS_RUNNING)
        sceKernelUsleep(16000);
    OrbisDialogResult result{};
    const bool accepted = sceImeDialogGetResult(&result) >= 0 &&
        result.endstatus == ORBIS_DIALOG_OK;
    sceImeDialogTerm();
    if (!accepted) return false;
    value.clear();
    for (int i = 0; i < 64 && buffer[i]; ++i)
        value.push_back(buffer[i] < 128 ? static_cast<char>(buffer[i]) : '?');
    return true;
}

}

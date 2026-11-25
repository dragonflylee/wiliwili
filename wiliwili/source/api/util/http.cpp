//
// Created by fang on 2022/5/1.
//

#include "bilibili/util/http.hpp"

#include <mutex>

#ifndef BOREALIS_USE_STD_THREAD
#include <pthread.h>
#endif

namespace bilibili {

namespace {

class MultiPool {
public:
    static CURLM* instance() { return get(true); }

    static void releaseThread() { get(false); }

private:
    // keep=false 时释放并返回 nullptr。PS4 libc 未导出 __cxa_thread_atexit_impl，
    // Switch 的 TLS 也不可靠，所以自己管理池的创建与销毁。
    static CURLM* get(bool keep) {
#ifdef BOREALIS_USE_STD_THREAD
        static thread_local CURLM* multi = keep ? create() : nullptr;
        return multi;
#else
        static pthread_key_t key = createKey();

        CURLM* multi = static_cast<CURLM*>(pthread_getspecific(key));
        if (keep && multi == nullptr) {
            multi = create();
            pthread_setspecific(key, multi);
        } else if (!keep && multi != nullptr) {
            pthread_setspecific(key, nullptr);
            destroy(multi);
        }
        return multi;
#endif
    }

    static CURLM* create() {
        CURLM* multi = curl_multi_init();
        if (multi) curl_multi_setopt(multi, CURLMOPT_MAXCONNECTS, 6L);
        return multi;
    }

    static void destroy(CURLM* multi) {
        if (multi) curl_multi_cleanup(multi);
    }

#ifndef BOREALIS_USE_STD_THREAD
    static pthread_key_t createKey() {
        static pthread_key_t key;
        static std::once_flag once;
        std::call_once(once, []() { pthread_key_create(&key, nullptr); });
        return key;
    }
#endif
};

}  // namespace

CURLM* HTTP::threadMulti() { return MultiPool::instance(); }

void HTTP::cleanupThread() { MultiPool::releaseThread(); }

cpr::Response HTTP::perform(cpr::Session& session, cpr::MultiPerform::HttpMethod method) {
    // 注意：这里用的是当前线程共享的 CURLM，调用方必须保证同一线程内
    // 不会有两个 in-flight transfer（有 multi 消息队列竞争）。
    // 目前所有调用都来自 cpr::async 线程池，同一 worker 串行执行。
    switch (method) {
        case cpr::MultiPerform::HttpMethod::GET_REQUEST:
            session.PrepareGet();
            break;
        case cpr::MultiPerform::HttpMethod::POST_REQUEST:
            session.PreparePost();
            break;
        case cpr::MultiPerform::HttpMethod::PUT_REQUEST:
            session.PreparePut();
            break;
        case cpr::MultiPerform::HttpMethod::DELETE_REQUEST:
            session.PrepareDelete();
            break;
        case cpr::MultiPerform::HttpMethod::PATCH_REQUEST:
            session.PreparePatch();
            break;
        case cpr::MultiPerform::HttpMethod::HEAD_REQUEST:
            session.PrepareHead();
            break;
        case cpr::MultiPerform::HttpMethod::OPTIONS_REQUEST:
            session.PrepareOptions();
            break;
        default:
            // UNDEFINED / DOWNLOAD_REQUEST 不在当前使用范围内，回退为 GET
            session.PrepareGet();
            break;
    }

    CURL* easy   = session.GetCurlHolder()->handle;
    CURLM* multi = MultiPool::instance();
    if (easy == nullptr || multi == nullptr) {
        cpr::Response r;
        r.error.message = "curl handle unavailable (out of memory)";
        return r;
    }

    // 每次传输都要 add/remove 一次，连接缓存挂在 multi 上不会丢
    CURLMcode mc = curl_multi_add_handle(multi, easy);
    if (mc != CURLM_OK) {
        cpr::Response r;
        r.error.message = std::string("curl multi: ") + curl_multi_strerror(mc);
        return r;
    }

    struct Detach {
        CURLM* multi;
        CURL* easy;
        ~Detach() { curl_multi_remove_handle(this->multi, this->easy); }
    } detach{multi, easy};

    CURLcode res = CURLE_OK;
    bool failed  = false;
    int running  = 0;
    for (;;) {
        mc = curl_multi_perform(multi, &running);
        if (mc != CURLM_OK) {
            failed = true;
            break;
        }
        if (!running) break;
        mc = curl_multi_poll(multi, nullptr, 0, 100, nullptr);
        if (mc != CURLM_OK) {
            failed = true;
            break;
        }
    }

    if (failed) {
        cpr::Response r;
        r.error.message = std::string("curl multi: ") + curl_multi_strerror(mc);
        return r;
    }

    bool done    = false;
    int pending  = 0;
    CURLMsg* msg = nullptr;
    while ((msg = curl_multi_info_read(multi, &pending)) != nullptr) {
        if (msg->msg == CURLMSG_DONE && msg->easy_handle == easy) {
            res  = msg->data.result;
            done = true;
        }
    }
    if (!done) {
        cpr::Response r;
        r.error.message = "curl multi: transfer result missing";
        return r;
    }

    return session.Complete(res);
}

std::string HTTP::getEncodedCookie(const cpr::Cookies& cookies) {
    std::stringstream stream;
    for (auto it = cookies.begin(); it != cookies.end(); ++it) {
        const cpr::Cookie& item = *it;
        stream << item.GetName() << "=" << item.GetValue();
        if (std::next(it) != cookies.end()) stream << "; ";
    }
    return stream.str();
}

void HTTP::signParameters(cpr::Parameters& parameters) {
    parameters.Add({{"appkey", BILIBILI_APP_KEY},
                    {"build", BILIBILI_BUILD},
                    {"ts", std::to_string(wiliwili::getUnixTime() * 1000)}});
    std::vector<std::string> kv;
    pystring::split(parameters.GetContent(cpr::CurlHolder()), kv, "&");
    std::sort(kv.begin(), kv.end());
    parameters.Add({{"sign", websocketpp::md5::md5_hash_hex(pystring::join("&", kv) + BILIBILI_APP_SECRET)}});
}

};  // namespace bilibili
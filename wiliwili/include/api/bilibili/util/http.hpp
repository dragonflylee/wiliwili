//
// Created by fang on 2022/5/1.
//

#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>
#include <cpr/multiperform.h>

#include "bilibili/util/md5.hpp"
#include "bilibili/util/json.hpp"
#include "bilibili/util/wbi.hpp"
#include "utils/number_helper.hpp"
#include <pystring.h>

namespace bilibili {

using Cookies = std::map<std::string, std::string>;

const std::string BILIBILI_APP_KEY    = "aa1e74ee4874176e";
const std::string BILIBILI_APP_SECRET = "54e6a9a31b911cd5fc0daa66ebf94bc4";
const std::string BILIBILI_BUILD      = "1001011000";

using ErrorCallback = std::function<void(const std::string&, int code)>;
#define ERROR_MSG(msg, ...) \
    if (error) error(msg, __VA_ARGS__)
#define HTTP_CALLBACK(data) \
    if (callback) callback(data)

class HTTP {
public:
    static inline cpr::Cookies COOKIES = {false};
    static inline cpr::Header HEADERS  = {
        {"User-Agent", "wiliwili"},
        {"Referer", "https://www.bilibili.com/client"},
        {"Origin", "https://www.bilibili.com"},
    };
    static inline int TIMEOUT = 10000;
    static inline int CONNECTION_TIMEOUT = 0;
    static inline int DNS_CACHE_TIMEOUT = 60;
    static inline cpr::Proxies PROXIES;
    static inline cpr::VerifySsl VERIFY;
    static inline std::string PROTOCOL = "https:";

    static CURLM* threadMulti();

    static void cleanupThread();

    static cpr::Response perform(cpr::Session& session, cpr::MultiPerform::HttpMethod method);

    static void getAsync(std::shared_ptr<cpr::Session> session, const std::function<void(const cpr::Response&)>& then) {
        if (!session) return;
        cpr::async([session, then]() {
            const cpr::Response r = perform(*session, cpr::MultiPerform::HttpMethod::GET_REQUEST);
            if (then) then(r);
        });
    }

    static void postAsync(std::shared_ptr<cpr::Session> session, const std::function<void(const cpr::Response&)>& then) {
        if (!session) return;
        cpr::async([session, then]() {
            const cpr::Response r = perform(*session, cpr::MultiPerform::HttpMethod::POST_REQUEST);
            if (then) then(r);
        });
    }

    static std::string getEncodedCookie(const cpr::Cookies& cookies);

    static std::shared_ptr<cpr::Session> createSession() {
        auto session = std::make_shared<cpr::Session>();
        CURL* curl = session->GetCurlHolder()->handle;
        curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, HTTP::DNS_CACHE_TIMEOUT);
        session->SetTimeout(cpr::Timeout{bilibili::HTTP::TIMEOUT});
        session->SetConnectTimeout(cpr::ConnectTimeout{bilibili::HTTP::CONNECTION_TIMEOUT});
        session->SetHeader(bilibili::HTTP::HEADERS);
        session->SetProxies(bilibili::HTTP::PROXIES);
        session->SetVerifySsl(bilibili::HTTP::VERIFY);
        return session;
    }

    static void _cpr_post(const std::string& url, const cpr::Parameters& parameters = {},
                           const cpr::Payload& payload                               = {},
                           const std::function<void(const cpr::Response&)>& callback = nullptr,
                           const ErrorCallback& error                                = nullptr) {
        auto session = createSession();
        session->SetUrl(cpr::Url{parseLink(url)});
        session->SetParameters(parameters);
        session->SetPayload(payload);

        cpr::async([session, callback, error]() {
            const cpr::Response r = perform(*session, cpr::MultiPerform::HttpMethod::POST_REQUEST);
            if (r.error) {
                ERROR_MSG(r.error.message, -1);
                return;
            } else if (r.status_code != 200) {
                ERROR_MSG("Network error. [Status code: " + std::to_string(r.status_code) + " ]", r.status_code);
                return;
            }
            if (callback) callback(r);
        });
    }

    static void _cpr_get(const std::string& url, const cpr::Parameters& parameters = {},
                         const std::function<void(const cpr::Response&)>& callback = nullptr,
                         const ErrorCallback& error                               = nullptr) {
        auto session = createSession();
        session->SetUrl(cpr::Url{parseLink(url)});
        session->SetParameters(parameters);

        cpr::async([session, callback, error]() {
            const cpr::Response r = perform(*session, cpr::MultiPerform::HttpMethod::GET_REQUEST);
            if (r.error) {
                ERROR_MSG(r.error.message, -1);
                return;
            }
            if (r.status_code != 200) {
                ERROR_MSG("Network error. [Status code: " + std::to_string(r.status_code) + " ]", r.status_code);
                return;
            }
            if (callback) callback(r);
        });
    }

    template <typename ReturnType>
    static int parseJson(const cpr::Response& r, const std::function<void(ReturnType)>& callback = nullptr,
                          const ErrorCallback& error = nullptr) {
        try {
            nlohmann::json res = nlohmann::json::parse(r.text);
            int code           = res.at("code").get<int>();
            if (code == 0) {
                if (res.contains("data") && (res.at("data").is_object() || res.at("data").is_array())) {
                    HTTP_CALLBACK(res.at("data").get<ReturnType>());
                    return 0;
                } else if (res.contains("result") && res.at("result").is_object()) {
                    HTTP_CALLBACK(res.at("result").get<ReturnType>());
                    return 0;
                } else {
                    printf("data: %s\n", r.text.c_str());
                    ERROR_MSG("Cannot find data", -1);
                }
            } else if (res.at("message").is_string()) {
                ERROR_MSG(res.at("message").get<std::string>(), code);
            } else {
                ERROR_MSG("Param error", -1);
            }
        } catch (const std::exception& e) {
            ERROR_MSG("Api error. \n" + std::string{e.what()}, 200);
            printf("data: %s\n", r.text.c_str());
            printf("ERROR: %s\n", e.what());
        }
        return 1;
    }

    static void signParameters(cpr::Parameters& parameters);

    template <typename ReturnType>
    static void getResultAsync(const std::string& url,
                               cpr::Parameters parameters                      = {},
                               const std::function<void(ReturnType)>& callback = nullptr,
                               const ErrorCallback& error                      = nullptr,
                               bool needSign                                   = false) {
        if (needSign) {
            signParameters(parameters);
        }
        _cpr_get(
            url,
            parameters,
            [callback, error](const cpr::Response& r) {
                parseJson<ReturnType>(r, callback, error);
            },
            error);
    }

    template <typename ReturnType>
    static void getResultWithWbiAsync(const std::string& url,
                                      cpr::Parameters parameters                      = {},
                                      const std::function<void(ReturnType)>& callback = nullptr,
                                      const ErrorCallback& error                      = nullptr,
                                      bool needSign                                   = false) {
        wbi::updateWbiKeys([url, parameters, callback, error, needSign]() mutable {
            if (needSign) {
                signParameters(parameters);
            }
            wbi::encWbi(parameters);
            _cpr_get(
                url,
                parameters,
                [callback, error](const cpr::Response& r) {
                    parseJson<ReturnType>(r, callback, error);
                },
                error);
        }, error);
    }

    template <typename ReturnType>
    static void postResultAsync(const std::string& url,
                                cpr::Parameters parameters                      = {},
                                const cpr::Payload& payload                     = {},
                                const std::function<void(ReturnType)>& callback = nullptr,
                                const ErrorCallback& error                      = nullptr,
                                bool needSign                                   = false) {
        if (needSign) {
            signParameters(parameters);
        }
        _cpr_post(
            url, parameters, payload,
            [callback, error](const cpr::Response& r) {
                try {
                    nlohmann::json res = nlohmann::json::parse(r.text);
                    const int code     = res.at("code").get<int>();
                    if (code == 0) {
                        if (res.contains("data") && res.at("data").is_object()) {
                            HTTP_CALLBACK(res.at("data").get<ReturnType>());
                        } else if (res.contains("result") && res.at("result").is_object()) {
                            HTTP_CALLBACK(res.at("result").get<ReturnType>());
                        } else {
                            printf("data: %s\n", r.text.c_str());
                            ERROR_MSG("Cannot find data", -1);
                        }
                        return;
                    }
                    ERROR_MSG(res.at("message").get<std::string>(), code);
                } catch (const std::exception& e) {
                    ERROR_MSG(std::string(e.what()), r.status_code);
                    printf("data: %s\n", r.text.c_str());
                    printf("ERROR: %s\n", e.what());
                }
            },
            error);
    }

    static void postResultAsync(const std::string& url,
                                const cpr::Parameters& parameters     = {},
                                const cpr::Payload& payload           = {},
                                const std::function<void()>& callback = nullptr,
                                const ErrorCallback& error            = nullptr) {
        _cpr_post(
            url, parameters, payload,
            [callback, error](const cpr::Response& r) {
                try {
                    nlohmann::json res = nlohmann::json::parse(r.text);
                    const int code     = res.at("code").get<int>();
                    if (code == 0) {
                        if (callback) callback();
                        return;
                    }
                    ERROR_MSG(res.at("message").get<std::string>(), code);
                } catch (const std::exception& e) {
                    ERROR_MSG(std::string(e.what()), r.status_code);
                    printf("data: %s\n", r.text.c_str());
                    printf("ERROR: %s\n", e.what());
                }
            },
            error);
    }
};

}  // namespace bilibili
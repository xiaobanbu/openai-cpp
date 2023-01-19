// The MIT License (MIT)
// 
// Copyright (c) 2023 Florian Dang
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef OPENAI_HPP_
#define OPENAI_HPP_

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sstream>
#include <mutex>
#include <cstdlib>

#ifndef CURL_STATICLIB
# include <curl/curl.h>
#else 
# include "curl/curl.h"
#endif

#include "json.hpp"  // nlohmann/json

#if OPENAI_VERBOSE_OUTPUT
# pragma message ("OPENAI_VERBOSE_OUTPUT is ON")
#endif

namespace openai {

namespace _detail {

// Json alias
using Json = nlohmann::json;

struct Response {
    std::string text;
    bool        is_error;
    std::string error_message;
};

// Simple curl Session inspired by CPR
class Session {
public:
    Session(bool throw_exception) : throw_exception_{throw_exception} {
        curl_global_init(CURL_GLOBAL_ALL);
        curl_ = curl_easy_init();
    }
    Session(bool throw_exception, std::string proxy_url) : throw_exception_{ throw_exception } {
        curl_global_init(CURL_GLOBAL_ALL);
        curl_ = curl_easy_init();
        setProxyUrl(proxy_url);
    }
    ~Session() { curl_easy_cleanup(curl_); curl_global_cleanup(); }

    void setUrl(const std::string& url) { url_ = url; }

    void setToken(const std::string& token, const std::string& organization) {
        token_ = token;
        organization_ = organization;
    }
 
    void setProxyUrl(const std::string& url) {
        proxy_url_ = url; 
        if (nullptr != curl_) {
            curl_easy_setopt(curl_, CURLOPT_PROXY, proxy_url_.c_str());
        }
    }

    void setBody(const std::string& data);
    Response get();
    Response post();
    Response makeRequest();
    std::string easyEscape(const std::string& text);

private:
    static size_t writeFunction(void* ptr, size_t size, size_t nmemb, std::string* data) {
        data->append((char*) ptr, size * nmemb);
        return size * nmemb;
    }

private:
    CURL*       curl_;
    CURLcode    res_;
    std::string url_;
    std::string proxy_url_;
    std::string token_;
    std::string organization_;

    bool        throw_exception_;
    std::mutex  mutex_request_;
};

inline void Session::setBody(const std::string& data) { 
    if (curl_) {
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, data.length());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, data.data());
    }
}

inline Response Session::get() {
    if (curl_) {
        curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl_, CURLOPT_POST, 0L);
        curl_easy_setopt(curl_, CURLOPT_NOBODY, 0L);
    }
    return makeRequest();
}

inline Response Session::post() {
    return makeRequest();
}

inline Response Session::makeRequest() {
    std::lock_guard<std::mutex> lock(mutex_request_);
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, std::string{"Authorization: Bearer " + token_}.c_str());
    if (!organization_.empty()) {
        headers = curl_slist_append(headers, std::string{"OpenAI-Organization: " + organization_}.c_str());
    }
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_, CURLOPT_URL, url_.c_str());
    
    std::string response_string;
    std::string header_string;
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeFunction);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_string);
    curl_easy_setopt(curl_, CURLOPT_HEADERDATA, &header_string);

    res_ = curl_easy_perform(curl_);

    bool is_error = false;
    std::string error_msg{};
    if(res_ != CURLE_OK) {
        is_error = true;
        error_msg = "curl_easy_perform() failed " + std::string{curl_easy_strerror(res_)};
        if (throw_exception_) 
            throw std::runtime_error(error_msg);
        else 
            std::cerr << "[OpenAI] curl_easy_perform() failed " << error_msg << '\n';
    }

    return { response_string, is_error, error_msg };
}

inline std::string Session::easyEscape(const std::string& text) {
    // char *encoded_output = curl_easy_escape(curl_, text.c_str(), static_cast<int>(text.length()));
    // return std::string{encoded_output};
    char *encoded_output = curl_easy_escape(curl_, text.c_str(), static_cast<int>(text.length()));
    const auto str = std::string{ encoded_output };
    curl_free(encoded_output);
    return str;
}

// forward declaration for category structures
class  OpenAI;

struct Channel;
struct User;

// https://beta.openai.com/docs/api-reference/models
// List and describe the various models available in the API. You can refer to the Models documentation to understand what models are available and the differences between them.
struct CategoryModel {
    Json list();
    Json retrieve(const std::string& model);

    CategoryModel(OpenAI& openai) : openai_{openai} {}
private:
    OpenAI& openai_;
};

// https://beta.openai.com/docs/api-reference/completions
// Given a prompt, the model will return one or more predicted completions, and can also return the probabilities of alternative tokens at each position.
struct CategoryCompletion {
    Json create(Json input);

    CategoryCompletion(OpenAI& openai) : openai_{openai} {}

private:
    OpenAI& openai_;
};

// https://beta.openai.com/docs/api-reference/edits
// Given a prompt and an instruction, the model will return an edited version of the prompt.
struct CategoryEdit {
    Json create(Json input);

    CategoryEdit(OpenAI& openai) : openai_{openai} {}

private:
    OpenAI& openai_;
};


// https://beta.openai.com/docs/api-reference/images
// Given a prompt and/or an input image, the model will generate a new image.
struct CategoryImage {
    Json create(Json input);
    Json edit(Json input);
    Json variation(Json input);

    CategoryImage(OpenAI& openai) : openai_{openai} {}

private:
    OpenAI& openai_;
};

// https://beta.openai.com/docs/api-reference/embeddings
// Get a vector representation of a given input that can be easily consumed by machine learning models and algorithms.
struct CategoryEmbedding {
    Json create(Json input);
    CategoryEmbedding(OpenAI& openai) : openai_{openai} {}

private:
    OpenAI& openai_;
};

// https://beta.openai.com/docs/api-reference/files
// Files are used to upload documents that can be used with features like Fine-tuning.
struct CategoryFile {
    Json list();
    Json upload(Json input);
    // Json del(const std::string& file); // TODO
    Json retrieve(const std::string& file_id);
    Json content(const std::string& file_id);

    CategoryFile(OpenAI& openai) : openai_{openai} {}

private:
    OpenAI& openai_;
};

// https://beta.openai.com/docs/api-reference/fine-tunes
// Manage fine-tuning jobs to tailor a model to your specific training data.
struct CategoryFineTune {
    Json create(Json input);
    Json list();
    Json retrieve(const std::string& fine_tune_id);
    Json content(const std::string& fine_tune_id);
    Json cancel(const std::string& fine_tune_id);
    Json events(const std::string& fine_tune_id);
    // Json del(const std::string& model); // TODO

    CategoryFineTune(OpenAI& openai) : openai_{openai} {}

private:
    OpenAI& openai_;
};

// https://beta.openai.com/docs/api-reference/moderations
// Given a input text, outputs if the model classifies it as violating OpenAI's content policy.
struct CategoryModeration {
    Json create(Json input);

    CategoryModeration(OpenAI& openai) : openai_{openai} {}

private:
    OpenAI& openai_;
};


class OpenAI {
public:
    OpenAI() = delete;
    OpenAI(const std::string& token, const std::string& organization = "", bool throw_exception = true) 
        : session_{throw_exception}, token_{token}, organization_{organization}, throw_exception_{throw_exception} {
            session_.setUrl("https://api.openai.com/v1/");
            session_.setToken(token_, organization_);
        }
    
    OpenAI(const OpenAI&)               = delete;
    OpenAI& operator=(const OpenAI&)    = delete;
    OpenAI(OpenAI&&)                    = delete;
    OpenAI& operator=(OpenAI&&)         = delete;

    void set_proxy(const std::string& url) { session_.setProxyUrl(url); }

    // void change_token(const std::string& token) { token_ = token; };
    void set_throw_exception(bool throw_exception) { throw_exception_ = throw_exception; }

    Json post(const std::string& suffix, const std::string& data = "") {
        setParameters(suffix, data);
        auto response = session_.post();
        if (response.is_error){ 
            trigger_error(response.error_message);
        }

        Json json{};
        if (isJson(response.text)){

            json = Json::parse(response.text); 
            checkResponse(json);
        }
        else{
          #if OPENAI_VERBOSE_OUTPUT
            std::cerr << "Response is not a valid JSON";
            std::cout << "<< " << response.text << "\n";
          #endif
        }
       
        return json;
    }

    Json get(const std::string& suffix, const std::string& data = "") {
        setParameters(suffix, data);
        auto response = session_.get();
        if (response.is_error) { trigger_error(response.error_message); }

        Json json{};
        if (isJson(response.text)) {
            json = Json::parse(response.text);
            checkResponse(json);
        }
        else {
          #if OPENAI_VERBOSE_OUTPUT
            std::cerr << "Response is not a valid JSON\n";
            std::cout << "<< " << response.text<< "\n";
          #endif
        }
        return json;
    }

    Json post(const std::string& suffix, const Json& json) {
        return post(suffix, json.dump());
    }

    // Json get(const std::string& suffix, const Json& json) {
        // auto elements = json_to_elements(json);
        // return get(suffix, join(elements));
    // }

    std::string easyEscape(const std::string& text) { return session_.easyEscape(text); }

    void debug() const { std::cout << token_ << '\n'; }

    void setBaseUrl(const std::string &url) {
        base_url = url;
    }

    std::string getBaseUrl() const {
        return base_url;
    }
private:
    std::string base_url{ "https://api.openai.com/v1/" };

    void setParameters(const std::string& suffix, const std::string& data = "") {
        auto complete_url =  base_url+ suffix;
        session_.setUrl(complete_url);
        session_.setBody(data);

#if OPENAI_VERBOSE_OUTPUT
        std::cout << ">> request: "<< complete_url << "  " << data << '\n';
#endif
    }

    void checkResponse(const Json& json) {
        if (json.count("error")) {
            auto reason = json["error"].dump();
            trigger_error(reason);
#if SLACKING_VERBOSE_OUTPUT
            std::cerr << "<< response:\n" << json.dump(2) << "\n";
#endif
        } 
    }

    // as of now the only way
    bool isJson(const std::string &data){
        bool rc = true;
        try {
            auto json = Json::parse(data); // throws if no json 
        }
        catch (std::exception &){
            rc = false;
        }
        return(rc);
    }

    void trigger_error(const std::string& msg) {
        if (throw_exception_) 
            throw std::runtime_error(msg);
        else 
            std::cerr << "[OpenAI] error. Reason: " << msg << '\n';
    }

private:
    Session    session_;

public:
    std::string             token_;
    std::string             organization_;
    bool                    throw_exception_;
    CategoryModel           model     {*this};
    CategoryCompletion      completion{*this};
    CategoryEdit            edit      {*this};
    CategoryImage           image     {*this};
    CategoryEmbedding       embedding {*this};
    CategoryFile            file      {*this};
    CategoryFineTune        fine_tune {*this};
    CategoryModeration      moderation{*this};
    // CategoryEngine          engine{*this}; // Not handled since deprecated (use Model instead)
};

inline std::string bool_to_string(const bool b) {
    std::ostringstream ss;
    ss << std::boolalpha << b;
    return ss.str();
}

inline OpenAI& configure(const std::string& token, const std::string& organization = "", bool throw_exception = true)  {
    static OpenAI instance{token, organization, throw_exception};
    return instance;
}

inline OpenAI& instance() {
    return configure("");
}

inline Json post(const std::string& suffix, const Json& json) {
    return instance().post(suffix, json);
}

inline Json get(const std::string& suffix/*, const Json& json*/) {
    return instance().get(suffix);
}

// Helper functions to get category structures instance()

inline CategoryModel& model() {
    return instance().model;
}

inline CategoryCompletion& completion() {
    return instance().completion;
}

inline CategoryEdit& edit() {
    return instance().edit;
}

inline CategoryImage& image() {
    return instance().image;
}

inline CategoryEmbedding& embedding() {
    return instance().embedding;
}

inline CategoryFile& file() {
    return instance().file;
}

inline CategoryFineTune& fineTune() {
    return instance().fine_tune;
}

inline CategoryModeration& moderation() {
    return instance().moderation;
}

// Definitions of category methods

// GET https://api.openai.com/v1/models
// Lists the currently available models, and provides basic information about each one such as the owner and availability.
inline Json CategoryModel::list() {
    return openai_.get("models");
}

// GET https://api.openai.com/v1/models/{model}
// Retrieves a model instance, providing basic information about the model such as the owner and permissioning.
inline Json CategoryModel::retrieve(const std::string& model) {
    return openai_.get("models/" + model);
}

// POST https://api.openai.com/v1/completions
// Creates a completion for the provided prompt and parameters
inline Json CategoryCompletion::create(Json input) {
    return openai_.post("completions", input);
}

// POST https://api.openai.com/v1/edits
// Creates a new edit for the provided input, instruction, and parameters
inline Json CategoryEdit::create(Json input) {
    return openai_.post("edits", input);
}

// POST https://api.openai.com/v1/images/generations
// Given a prompt and/or an input image, the model will generate a new image.
inline Json CategoryImage::create(Json input) {
    return openai_.post("images/generations", input);
}

// POST https://api.openai.com/v1/images/edits
// Creates an edited or extended image given an original image and a prompt.
inline Json CategoryImage::edit(Json input) {
    return openai_.post("images/edits", input);
}

// POST https://api.openai.com/v1/images/variations
// Creates a variation of a given image.
inline Json CategoryImage::variation(Json input) {
    return openai_.post("images/variations", input);
}

inline Json CategoryEmbedding::create(Json input) { 
    return openai_.post("embeddings", input); 
}

inline Json CategoryFile::list() { 
    return openai_.get("files"); 
}

inline Json CategoryFile::upload(Json input) { 
    return openai_.post("files", input); 
}

inline Json CategoryFile::retrieve(const std::string& file_id) { 
    return openai_.get("files/" + file_id); 
}

inline Json CategoryFile::content(const std::string& file_id) { 
    return openai_.get("files/" + file_id + "/content"); 
}

inline Json CategoryFineTune::create(Json input) { 
    return openai_.post("fine-tunes", input); 
}

inline Json CategoryFineTune::list() { 
    return openai_.get("fine-tunes"); 
}

inline Json CategoryFineTune::retrieve(const std::string& file_id) { 
    return openai_.get("fine-tunes/" + file_id); 
}

inline Json CategoryFineTune::content(const std::string& file_id) { 
    return openai_.get("fine-tunes/" + file_id + "/content"); 
}

inline Json CategoryFineTune::cancel(const std::string& file_id) { 
    return openai_.get("fine-tunes/" + file_id + "/cancel"); 
}

inline Json CategoryFineTune::events(const std::string& file_id) { 
    return openai_.get("fine-tunes/" + file_id + "/events"); 
}

inline Json CategoryModeration::create(Json input) { 
    return openai_.post("moderations", input); 
}

} // namespace _detail

// Public interface

// using _detail::operator<<;
using _detail::OpenAI;

// instance
using _detail::configure;
using _detail::instance;

// Generic methods
using _detail::post;
using _detail::get;

// Helper categories access
using _detail::model;
using _detail::completion;
using _detail::edit;
using _detail::image;
using _detail::embedding;
using _detail::file;
using _detail::fineTune;
using _detail::moderation;

using _detail::Json;

} // namespace openai

#endif // OPENAI_HPP_
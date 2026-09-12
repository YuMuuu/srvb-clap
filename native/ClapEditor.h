#pragma once

#include <clap/ext/gui.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace choc::ui
{
class WebView;
}

class ClapEditor
{
public:
    struct Callbacks
    {
        std::function<void ()> ready;
        std::function<void (std::optional<std::string>)> reload;
        std::function<void (const std::string&, double)> setParameter;
    };

    ClapEditor (std::filesystem::path assetDirectory, Callbacks callbacks);
    ~ClapEditor ();

    ClapEditor (const ClapEditor&) = delete;
    ClapEditor& operator= (const ClapEditor&) = delete;

    bool setParent (const clap_window_t& parent);
    void setSize (uint32_t width, uint32_t height);
    void setVisible (bool visible);
    void evaluateJavascript (const std::string& script);

private:
    const Callbacks callbacks;
    std::unique_ptr<choc::ui::WebView> webView;
};

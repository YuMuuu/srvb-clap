#include "ClapEditor.h"

#include <choc_JSON.h>
#include <choc_WebView.h>

#include <fstream>
#include <iterator>
#include <unordered_map>
#include <vector>

#if CHOC_WINDOWS
#include <windows.h>
#endif

namespace
{
double numberFromChocValue (const choc::value::ValueView& value)
{
    if (value.isFloat32 ())
        return value.getFloat32 ();
    if (value.isFloat64 ())
        return value.getFloat64 ();
    if (value.isInt32 ())
        return value.getInt32 ();
    return static_cast<double> (value.getInt64 ());
}

bool isNumber (const choc::value::ValueView& value)
{
    return value.isFloat32 () || value.isFloat64 () || value.isInt32 () || value.isInt64 ();
}

std::string mimeTypeFor (const std::filesystem::path& path)
{
    static const std::unordered_map<std::string, std::string> mimeTypes{
        {".css", "text/css"},          {".html", "text/html"}, {".js", "application/javascript"},
        {".json", "application/json"}, {".png", "image/png"},  {".svg", "image/svg+xml"},
    };
    const auto match = mimeTypes.find (path.extension ().string ());
    return match == mimeTypes.end () ? "application/octet-stream" : match->second;
}

std::optional<choc::ui::WebView::Options::Resource> loadResource (const std::filesystem::path& assetDirectory,
                                                                  const std::string& requestPath)
{
    auto relativePath =
        requestPath == "/" ? std::filesystem::path{"index.html"} : std::filesystem::path{requestPath}.relative_path ();
    relativePath = relativePath.lexically_normal ();
    if (relativePath.empty () || *relativePath.begin () == "..")
        return std::nullopt;

    const auto path = assetDirectory / relativePath;
    std::ifstream file (path, std::ios::binary);
    if (!file)
        return std::nullopt;

    std::vector<uint8_t> bytes{std::istreambuf_iterator<char> (file), std::istreambuf_iterator<char> ()};
    if (file.bad ())
        return std::nullopt;
    return choc::ui::WebView::Options::Resource{std::move (bytes), mimeTypeFor (path)};
}

#if CHOC_APPLE
struct NativePoint
{
    double x;
    double y;
};
struct NativeSize
{
    double width;
    double height;
};
struct NativeRect
{
    NativePoint origin;
    NativeSize size;
};
#endif
} // namespace

ClapEditor::ClapEditor (std::filesystem::path newAssetDirectory, Callbacks newCallbacks)
    : callbacks (std::move (newCallbacks))
{
    choc::ui::WebView::Options options;
#if ELEM_DEV_LOCALHOST
    (void)newAssetDirectory;
    options.enableDebugMode = true;
#else
    options.fetchResource = [directory = std::move (newAssetDirectory)] (const auto& path)
    { return loadResource (directory, path); };
#endif
    webView = std::make_unique<choc::ui::WebView> (options);
    webView->bind ("__postNativeMessage__",
                   [this] (const choc::value::ValueView& arguments) -> choc::value::Value
                   {
                       if (!arguments.isArray () || arguments.size () == 0 || !arguments[0].isString ())
                           return {};

                       const auto eventName = arguments[0].getString ();
                       if (eventName == "ready")
                       {
                           callbacks.ready ();
                       }
#if ELEM_DEV_LOCALHOST
                       else if (eventName == "reload")
                       {
                           std::optional<std::string> source;
                           if (arguments.size () > 1 && arguments[1].isObject () &&
                               arguments[1].hasObjectMember ("source") && arguments[1]["source"].isString ())
                               source = std::string (arguments[1]["source"].getString ());
                           callbacks.reload (std::move (source));
                       }
#endif
                       else if (eventName == "setParameterValue" && arguments.size () > 1)
                       {
                           const auto event = arguments[1];
                           if (event.isObject () && event.hasObjectMember ("paramId") && event["paramId"].isString () &&
                               event.hasObjectMember ("value") && isNumber (event["value"]))
                               callbacks.setParameter (std::string (event["paramId"].getString ()),
                                                       numberFromChocValue (event["value"]));
                       }
                       return {};
                   });

#if ELEM_DEV_LOCALHOST
    webView->navigate ("http://localhost:5173");
#endif
}

ClapEditor::~ClapEditor ()
{
#if CHOC_APPLE
    if (webView)
    {
        choc::objc::AutoReleasePool pool;
        choc::objc::call<void> (static_cast<id> (webView->getViewHandle ()), "removeFromSuperview");
    }
#elif CHOC_WINDOWS
    if (webView)
        SetParent (static_cast<HWND> (webView->getViewHandle ()), nullptr);
#endif
}

bool ClapEditor::setParent (const clap_window_t& parent)
{
#if CHOC_APPLE
    if (!parent.api || std::string (parent.api) != CLAP_WINDOW_API_COCOA || !parent.cocoa)
        return false;
    choc::objc::AutoReleasePool pool;
    auto* const view = static_cast<id> (webView->getViewHandle ());
    choc::objc::call<void> (static_cast<id> (parent.cocoa), "addSubview:", view);
    return true;
#elif CHOC_WINDOWS
    if (!parent.api || std::string (parent.api) != CLAP_WINDOW_API_WIN32 || !parent.win32)
        return false;
    auto* const view = static_cast<HWND> (webView->getViewHandle ());
    auto style = GetWindowLongPtr (view, GWL_STYLE);
    SetWindowLongPtr (view, GWL_STYLE, (style & ~static_cast<LONG_PTR> (WS_POPUP)) | WS_CHILD);
    SetLastError (ERROR_SUCCESS);
    const auto previousParent = SetParent (view, static_cast<HWND> (parent.win32));
    return previousParent != nullptr || GetLastError () == ERROR_SUCCESS;
#else
    (void)parent;
    return false;
#endif
}

void ClapEditor::setSize (uint32_t width, uint32_t height)
{
#if CHOC_APPLE
    choc::objc::AutoReleasePool pool;
    choc::objc::call<void> (static_cast<id> (webView->getViewHandle ()), "setFrame:",
                            NativeRect{{0, 0}, {static_cast<double> (width), static_cast<double> (height)}});
#elif CHOC_WINDOWS
    SetWindowPos (static_cast<HWND> (webView->getViewHandle ()), nullptr, 0, 0, static_cast<int> (width),
                  static_cast<int> (height), SWP_NOZORDER | SWP_NOACTIVATE);
#else
    (void)width;
    (void)height;
#endif
}

void ClapEditor::setVisible (bool visible)
{
#if CHOC_APPLE
    choc::objc::AutoReleasePool pool;
    choc::objc::call<void> (static_cast<id> (webView->getViewHandle ()), "setHidden:", static_cast<BOOL> (!visible));
#elif CHOC_WINDOWS
    ShowWindow (static_cast<HWND> (webView->getViewHandle ()), visible ? SW_SHOW : SW_HIDE);
#else
    (void)visible;
#endif
}

void ClapEditor::evaluateJavascript (const std::string& script)
{
    webView->evaluateJavascript (script);
}

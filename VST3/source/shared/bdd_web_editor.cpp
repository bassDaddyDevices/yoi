//
//  bdd_web_editor.cpp
//  Bass Daddy Devices VST3 glue
//

#include "bdd_web_editor.h"

#include "bdd_platform.h"
#include "bdd_resources.h"

#include "choc/gui/choc_WebView.h"
#include "choc/text/choc_JSON.h"

#include <cmath>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace bdd::vst3 {

namespace {

constexpr uint32 kTimerInterval = 33;

/// Only the page's own files are served; the factory presets are compiled in too, but the page
/// gets those through the plug-in, never by fetching them.
bool isPageResource(std::string_view path) {
    return path.substr(0, 9) != "/factory/";
}

} // namespace

WebEditor::WebEditor(EditController* controller, Size size, bool debug)
    : EditorView(controller), mSize(size), mDebug(debug) {
    ViewRect initial(0, 0, size.idealWidth, size.idealHeight);
    setRect(initial);
}

WebEditor::~WebEditor() {
    // A host may release the view without removing it first.
    destroyWebView();
}

tresult PLUGIN_API WebEditor::isPlatformTypeSupported(FIDString type) {
#if defined(__APPLE__)
    return FIDStringsEqual(type, kPlatformTypeNSView) ? kResultTrue : kResultFalse;
#elif defined(_WIN32)
    return FIDStringsEqual(type, kPlatformTypeHWND) ? kResultTrue : kResultFalse;
#else
    (void)type;
    return kResultFalse;
#endif
}

tresult PLUGIN_API WebEditor::checkSizeConstraint(ViewRect* rect) {
    if (rect == nullptr) {
        return kInvalidArgument;
    }
    const int minimumWidth = int(std::lround(mSize.minimumWidth * mScale));
    const int minimumHeight = int(std::lround(mSize.minimumHeight * mScale));
    if (rect->getWidth() < minimumWidth) {
        rect->right = rect->left + minimumWidth;
    }
    if (rect->getHeight() < minimumHeight) {
        rect->bottom = rect->top + minimumHeight;
    }
    return kResultTrue;
}

tresult PLUGIN_API WebEditor::setContentScaleFactor(ScaleFactor factor) {
#if defined(__APPLE__)
    (void)factor;
    return kResultFalse;
#else
    if (!(factor > 0.0f) || factor == mScale) {
        return kResultTrue;
    }
    // Keep the same size in CSS pixels, now at the new pixel density.
    const ViewRect& current = getRect();
    const float ratio = factor / mScale;
    mScale = factor;
    ViewRect scaled(0, 0, int(std::lround(current.getWidth() * ratio)), int(std::lround(current.getHeight() * ratio)));
    if (plugFrame) {
        plugFrame->resizeView(this, &scaled);
    } else {
        setRect(scaled);
    }
    return kResultTrue;
#endif
}

tresult PLUGIN_API WebEditor::onSize(ViewRect* newSize) {
    const tresult result = EditorView::onSize(newSize);
    if (mWebView && newSize != nullptr) {
        platform::resizeView(mWebView->getViewHandle(), newSize->getWidth(), newSize->getHeight());
    }
    return result;
}

void WebEditor::attachedToParent() {
    EditorView::attachedToParent();
    opened();
    createWebView();
    mTimer = owned(Timer::create(this, kTimerInterval));
}

void WebEditor::removedFromParent() {
    if (mTimer) {
        mTimer->stop();
        mTimer = nullptr;
    }
    destroyWebView();
    closed();
    EditorView::removedFromParent();
}

void WebEditor::onTimer(Timer*) {
    if (mWebView && mPageReady) {
        tick();
    }
}

void WebEditor::send(const choc::value::ValueView& state) {
    if (!mWebView || !mPageReady) {
        return;
    }
    mWebView->evaluateJavascript("window.bdd && window.bdd.receive(" + choc::json::toString(state) + ");");
}

void WebEditor::createWebView() {
    choc::ui::WebView::Options options;
    options.enableDebugMode = mDebug;
    options.acceptsFirstMouseClick = true;
    options.transparentBackground = false;

    // Serve the page from the files compiled into the plug-in.
    options.fetchResource = [](const std::string& path) -> std::optional<choc::ui::WebView::Options::Resource> {
        const std::string wanted = (path.empty() || path == "/") ? std::string("/index.html") : path;
        const EmbeddedResource* resource = findResource(wanted);
        if (resource == nullptr || !isPageResource(wanted)) {
            return std::nullopt;
        }
        choc::ui::WebView::Options::Resource result;
        result.data.assign(resource->data, resource->data + resource->size);
        result.mimeType = resource->mimeType;
        return result;
    };

    mWebView = std::make_unique<choc::ui::WebView>(options);
    if (!mWebView->loadedOK()) {
        mWebView.reset();
        return;
    }

    // Every message is caught here: a malformed one from the page must never unwind into the host.
    mWebView->bind("bddPost", [this](const choc::value::ValueView& args) -> choc::value::Value {
        try {
            if (args.isArray() && args.size() > 0 && args[0].isObject() && args[0].hasObjectMember("type")
                && args[0]["type"].isString()) {
                handleMessage(std::string(args[0]["type"].getString()), args[0]);
            }
        } catch (...) {
        }
        return {};
    });

    const ViewRect& size = getRect();
    platform::attachView(systemWindow, mWebView->getViewHandle(), size.getWidth(), size.getHeight());
}

void WebEditor::destroyWebView() {
    mPageReady = false;
    if (mWebView) {
        platform::detachView(mWebView->getViewHandle());
        mWebView.reset();
    }
}

} // namespace bdd::vst3

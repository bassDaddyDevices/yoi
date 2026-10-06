//
//  bdd_web_editor.h
//  Bass Daddy Devices VST3 glue
//
//  A plug-in window showing the synth's HTML/CSS/JS editor in the system web view (WKWebView on
//  macOS, WebView2 on Windows) through choc. The page is the one the Audio Unit shows, served from
//  the files compiled into the plug-in. It talks through `window.bdd` (bdd-bridge.js), whose
//  messages arrive here through one bound function, `bddPost`; the plug-in answers by calling
//  `window.bdd.receive(state)`. Shared by every synth: subclasses decide what the messages mean.
//

#pragma once

#include "base/source/timer.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <memory>
#include <string>

namespace choc::ui { class WebView; }
namespace choc::value { class Value; class ValueView; }

namespace bdd::vst3 {

class WebEditor : public Steinberg::Vst::EditorView,
                  public Steinberg::IPlugViewContentScaleSupport,
                  public Steinberg::ITimerCallback {
public:
    struct Size {
        int idealWidth;
        int idealHeight;
        int minimumWidth;
        int minimumHeight;
    };

    WebEditor(Steinberg::Vst::EditController* controller, Size size, bool debug);
    ~WebEditor() override;

    OBJ_METHODS(WebEditor, EditorView)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::IPlugViewContentScaleSupport)
    END_DEFINE_INTERFACES(EditorView)
    REFCOUNT_METHODS(EditorView)

    // IPlugViewContentScaleSupport. Windows hosts use this for high-DPI screens; macOS works in
    // points and never needs it.
    Steinberg::tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) SMTG_OVERRIDE;

    // IPlugView
    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canResize() SMTG_OVERRIDE { return Steinberg::kResultTrue; }
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) SMTG_OVERRIDE;

    // CPluginView
    void attachedToParent() SMTG_OVERRIDE;
    void removedFromParent() SMTG_OVERRIDE;

    // ITimerCallback
    void onTimer(Steinberg::Timer* timer) SMTG_OVERRIDE;

protected:
    /// One message from the page: an object with a `type`. Main thread.
    virtual void handleMessage(const std::string& type, const choc::value::ValueView& message) = 0;

    /// About 30 times a second while the page is loaded. Main thread.
    virtual void tick() {}

    /// The page's window opened or closed (attached to or removed from the host's window).
    virtual void opened() {}
    virtual void closed() {}

    /// Calls `window.bdd.receive(state)` on the page. Does nothing until the page has said hello.
    void send(const choc::value::ValueView& state);

    /// Set by a subclass when the page says hello; until then nothing is sent.
    void setPageReady(bool ready) { mPageReady = ready; }
    bool pageReady() const { return mPageReady; }

private:
    void createWebView();
    void destroyWebView();

    Size mSize;
    bool mDebug;
    std::unique_ptr<choc::ui::WebView> mWebView;
    Steinberg::IPtr<Steinberg::Timer> mTimer;
    bool mPageReady = false;
    /// Host-supplied content scale (Windows). Window sizes are this many pixels per CSS pixel.
    float mScale = 1.0f;
};

} // namespace bdd::vst3

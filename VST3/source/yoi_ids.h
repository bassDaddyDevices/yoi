//
//  yoi_ids.h
//  YOI VST3
//
//  Class IDs, version and the names of the messages between processor and controller.
//

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace yoi {

// Never change these once a build has been released: hosts store sessions against them.
static const Steinberg::FUID kProcessorUID(0x705785BC, 0x1EE448C6, 0xB8256DF6, 0xB7E8A304);
static const Steinberg::FUID kControllerUID(0x25AC449A, 0x389F4AB3, 0x880DDEAF, 0xA0690C0A);

/// Controller -> processor: the drawing changed. Carries `kDrawingAttribute`, the points as
/// float triples (x, y, bend).
inline constexpr const char* kDrawingMessage = "YoiDrawing";
inline constexpr const char* kDrawingAttribute = "points";

#define YOI_VST3_CATEGORY "Instrument|Synth"

#define YOI_VERSION_MAJOR 1
#define YOI_VERSION_MINOR 0
#define YOI_VERSION_PATCH 0
#define YOI_VERSION_BUILD 1

#define YOI_STRINGIFY_(x) #x
#define YOI_STRINGIFY(x) YOI_STRINGIFY_(x)
#define YOI_VERSION_STR                                                                         \
    YOI_STRINGIFY(YOI_VERSION_MAJOR) "." YOI_STRINGIFY(YOI_VERSION_MINOR) "."                     \
    YOI_STRINGIFY(YOI_VERSION_PATCH) "." YOI_STRINGIFY(YOI_VERSION_BUILD)

#define YOI_COMPANY_NAME "Bass Daddy Devices"
#define YOI_COMPANY_WEB ""
#define YOI_COMPANY_EMAIL ""
#define YOI_PLUGIN_NAME "YOI"

} // namespace yoi

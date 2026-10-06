//
//  yoi_entry.cpp
//  YOI VST3
//
//  The plug-in factory the host loads.
//

#include "yoi_controller.h"
#include "yoi_ids.h"
#include "yoi_processor.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF(YOI_COMPANY_NAME, YOI_COMPANY_WEB, YOI_COMPANY_EMAIL)

    DEF_CLASS2(INLINE_UID_FROM_FUID(yoi::kProcessorUID),
               PClassInfo::kManyInstances,
               kVstAudioEffectClass,
               YOI_PLUGIN_NAME,
               Vst::kDistributable,
               YOI_VST3_CATEGORY,
               YOI_VERSION_STR,
               kVstVersionString,
               yoi::Processor::createInstance)

    DEF_CLASS2(INLINE_UID_FROM_FUID(yoi::kControllerUID),
               PClassInfo::kManyInstances,
               kVstComponentControllerClass,
               YOI_PLUGIN_NAME " Controller",
               0,
               "",
               YOI_VERSION_STR,
               kVstVersionString,
               yoi::Controller::createInstance)

END_FACTORY

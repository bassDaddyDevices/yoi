//
//  bdd_spec_parameter.h
//  Bass Daddy Devices VST3 glue
//
//  A VST3 parameter described by a `ParamSpec`, so the host sees the same range, steps, units and
//  value text the kernel works in and the Audio Unit shows.
//

#pragma once

#include "bdd_param_spec.h"

#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ustring.h"

#include <cstdlib>
#include <functional>
#include <string>

namespace bdd::vst3 {

class SpecParameter : public Steinberg::Vst::Parameter {
public:
    /// `formatter`, when set, supplies the text for a plain value instead of `formatValue`: for
    /// readings only the plug-in knows (a percent that means milliseconds, say).
    SpecParameter(const ParamSpec& spec, Steinberg::int32 flags,
                  std::function<std::string(float)> formatter = {})
        : mSpec(spec), mFormatter(std::move(formatter)) {
        using namespace Steinberg;
        UString(info.title, str16BufferSize(Vst::String128)).fromAscii(spec.name.c_str());
        UString(info.shortTitle, str16BufferSize(Vst::String128)).fromAscii(spec.name.c_str());
        UString(info.units, str16BufferSize(Vst::String128)).fromAscii(unitLabel(spec.unit));
        info.id = spec.id;
        info.stepCount = spec.stepCount();
        info.defaultNormalizedValue = toNormalised(spec, spec.defaultValue);
        info.unitId = Vst::kRootUnitId;
        info.flags = flags;
        if (spec.unit == Unit::indexed) {
            info.flags |= Vst::ParameterInfo::kIsList;
        }
        setNormalized(info.defaultNormalizedValue);
    }

    const ParamSpec& spec() const { return mSpec; }

    std::string text(float plain) const {
        return mFormatter ? mFormatter(plain) : formatValue(mSpec, plain);
    }

    void toString(Steinberg::Vst::ParamValue normalised, Steinberg::Vst::String128 string) const SMTG_OVERRIDE {
        Steinberg::UString(string, str16BufferSize(Steinberg::Vst::String128))
            .fromAscii(text(bdd::vst3::toPlain(mSpec, normalised)).c_str());
    }

    bool fromString(const Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& normalised) const SMTG_OVERRIDE {
        char ascii[128] = {};
        Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(string), Steinberg::tstrlen(string))
            .toAscii(ascii, sizeof(ascii));
        const std::string typed(ascii);

        if (mSpec.unit == Unit::boolean && (typed == "On" || typed == "on" || typed == "Off" || typed == "off")) {
            normalised = (typed == "On" || typed == "on") ? 1.0 : 0.0;
            return true;
        }
        for (size_t index = 0; index < mSpec.options.size(); ++index) {
            if (typed == mSpec.options[index]) {
                normalised = toNormalised(mSpec, mSpec.minimum + float(index));
                return true;
            }
        }

        char* end = nullptr;
        double value = std::strtod(ascii, &end);
        if (end == ascii) {
            return false;
        }
        while (end != nullptr && *end == ' ') {
            ++end;
        }
        // Read back the way values are shown: "1.20 kHz", "2.00 s".
        if (end != nullptr && (*end == 'k' || *end == 'K') && mSpec.unit == Unit::hertz) {
            value *= 1000.0;
        } else if (end != nullptr && *end == 's' && mSpec.unit == Unit::milliseconds) {
            value *= 1000.0;
        }
        normalised = toNormalised(mSpec, float(value));
        return true;
    }

    Steinberg::Vst::ParamValue toPlain(Steinberg::Vst::ParamValue normalised) const SMTG_OVERRIDE {
        return bdd::vst3::toPlain(mSpec, normalised);
    }

    Steinberg::Vst::ParamValue toNormalized(Steinberg::Vst::ParamValue plain) const SMTG_OVERRIDE {
        return toNormalised(mSpec, float(plain));
    }

private:
    const ParamSpec& mSpec;
    std::function<std::string(float)> mFormatter;
};

} // namespace bdd::vst3

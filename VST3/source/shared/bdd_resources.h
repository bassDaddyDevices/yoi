//
//  bdd_resources.h
//  Bass Daddy Devices VST3 glue
//
//  Files compiled into the plug-in by cmake/embed_resources.cmake: the editor's page (served at
//  "/index.html" and so on) and the factory presets (at "/factory/<file>.json").
//

#pragma once

#include <cstddef>
#include <string_view>

namespace bdd::vst3 {

struct EmbeddedResource {
    const char* path;       ///< As requested, e.g. "/bdd-bridge.js".
    const char* mimeType;
    const unsigned char* data;
    size_t size;

    std::string_view text() const { return std::string_view(reinterpret_cast<const char*>(data), size); }
};

/// The resource at `path`, or nullptr.
const EmbeddedResource* findResource(std::string_view path);

/// Every resource, in the order they were embedded.
const EmbeddedResource* resourcesBegin();
const EmbeddedResource* resourcesEnd();

} // namespace bdd::vst3

#pragma once
// HkxXmlReader.h — Parse Havok XML packfile format into ParseResult.
//
// The XML format is a serialized object graph with __types__ and __data__
// sections. Objects reference each other by name (#nnnn). This reader
// parses the __data__ section and converts physics, scene, and shape
// objects into the same ParseResult structure used by binary/tagged readers.

#include "havok/types/hkx_types.h"

#include <string>

namespace Hkx {

class HkxXmlReader {
public:
    // Parse an XML packfile string and populate a ParseResult.
    ParseResult Parse(const std::string& xmlContent);

    // Parse an XML packfile from a file path.
    ParseResult ParseFile(const std::string& filePath);
};

} // namespace Hkx

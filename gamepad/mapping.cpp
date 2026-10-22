#include "gamepad/mapping.h"

#include <vector>

namespace allnes::gamepad
{
namespace
{
std::vector<std::string> splitFields(const std::string& mapping)
{
    std::vector<std::string> fields;
    std::string              current;

    for (char c : mapping)
    {
        if (c == ',')
        {
            if (!current.empty())
                fields.push_back(current);
            current.clear();
        }
        else
        {
            current += c;
        }
    }

    if (!current.empty())
        fields.push_back(current);
    return fields;
}

bool isDpadField(const std::string& field)
{
    return field.rfind("dpup:", 0) == 0 || field.rfind("dpdown:", 0) == 0 ||
           field.rfind("dpleft:", 0) == 0 || field.rfind("dpright:", 0) == 0;
}

// Complete D-pad bindings available on this device, or an empty string.
std::string dpadFields(MappingCounts counts)
{
    if (counts.hats >= 1)
        return "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8";

    // Xbox 360 pads on macOS expose the D-pad as the last four buttons.
    if (counts.buttons >= 15)
        return "dpup:b11,dpdown:b12,dpleft:b13,dpright:b14";

    return std::string();
}
} // namespace

DpadCoverage parseDpadCoverage(const std::string& mapping)
{
    DpadCoverage coverage;
    for (const std::string& field : splitFields(mapping))
    {
        if (field.rfind("dpup:", 0) == 0)
            coverage.up = true;
        else if (field.rfind("dpdown:", 0) == 0)
            coverage.down = true;
        else if (field.rfind("dpleft:", 0) == 0)
            coverage.left = true;
        else if (field.rfind("dpright:", 0) == 0)
            coverage.right = true;
    }
    return coverage;
}

std::string buildGenericMapping(const std::string& guid, MappingCounts counts)
{
    std::string mapping = guid + ",Generic Gamepad";

    const auto append = [&mapping](const std::string& field) {
        mapping += ',';
        mapping += field;
    };

    const auto addButton = [&append, &counts](const char* field, int index) {
        if (counts.buttons > index)
            append(std::string(field) + ":b" + std::to_string(index));
    };

    addButton("a", 0);
    addButton("b", 1);
    addButton("x", 2);
    addButton("y", 3);
    addButton("leftshoulder", 4);
    addButton("rightshoulder", 5);
    addButton("back", 6);
    addButton("start", 7);
    addButton("leftstick", 8);
    addButton("rightstick", 9);
    addButton("guide", 10);

    const auto addAxis = [&append, &counts](const char* field, int index) {
        if (counts.axes > index)
            append(std::string(field) + ":a" + std::to_string(index));
    };

    addAxis("leftx", 0);
    addAxis("lefty", 1);
    addAxis("rightx", 2);
    addAxis("righty", 3);
    addAxis("lefttrigger", 4);
    addAxis("righttrigger", 5);

    const std::string dpad = dpadFields(counts);
    if (!dpad.empty())
        mapping += ',' + dpad;

    mapping += ',';
    return mapping;
}

std::string completeDpadMapping(const std::string& mapping, MappingCounts counts)
{
    if (parseDpadCoverage(mapping).complete())
        return std::string();

    const std::string dpad = dpadFields(counts);
    if (dpad.empty())
        return std::string();

    std::vector<std::string> fields = splitFields(mapping);
    if (fields.size() < 2) // guid,name are mandatory
        return std::string();

    std::string result;
    bool        inserted = false;

    for (const std::string& field : fields)
    {
        if (isDpadField(field))
            continue;

        // Keep the platform field last, as in the community database.
        if (!inserted && field.rfind("platform:", 0) == 0)
        {
            result += dpad;
            result += ',';
            inserted = true;
        }

        result += field;
        result += ',';
    }

    if (!inserted)
    {
        result += dpad;
        result += ',';
    }

    return result;
}
} // namespace allnes::gamepad

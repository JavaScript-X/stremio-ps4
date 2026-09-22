#include "catalog.h"

#include <cctype>

namespace {
bool readJsonString(
    const std::string& text, size_t start, std::string& value, size_t& end) {
    if (start >= text.size() || text[start] != '"') return false;
    value.clear();
    for (size_t position = start + 1; position < text.size(); ++position) {
        const char current = text[position];
        if (current == '"') {
            end = position + 1;
            return true;
        }
        if (current != '\\') {
            value.push_back(current);
            continue;
        }
        if (++position >= text.size()) return false;
        switch (text[position]) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'u':
                if (position + 4 >= text.size()) return false;
                position += 4;
                value.push_back('?');
                break;
            default: return false;
        }
    }
    return false;
}

bool findStringField(
    const std::string& object, const char* field, std::string& value) {
    const std::string key = std::string("\"") + field + "\"";
    size_t position = object.find(key);
    if (position == std::string::npos) return false;
    position = object.find(':', position + key.size());
    if (position == std::string::npos) return false;
    do {
        ++position;
    } while (position < object.size() &&
             std::isspace(static_cast<unsigned char>(object[position])));
    size_t end = 0;
    return readJsonString(object, position, value, end);
}
}  // namespace

bool parseCatalogItems(
    const std::string& json,
    std::vector<CatalogItem>& items,
    size_t maximumItems) {
    items.clear();
    const size_t metas = json.find("\"metas\"");
    if (metas == std::string::npos) return false;
    size_t position = json.find('[', metas);
    if (position == std::string::npos) return false;

    bool inString = false;
    bool escaped = false;
    int depth = 0;
    size_t objectStart = std::string::npos;
    for (++position; position < json.size() && items.size() < maximumItems;
         ++position) {
        const char current = json[position];
        if (inString) {
            if (escaped) escaped = false;
            else if (current == '\\') escaped = true;
            else if (current == '"') inString = false;
            continue;
        }
        if (current == '"') inString = true;
        else if (current == '{') {
            if (depth++ == 0) objectStart = position;
        } else if (current == '}' && depth > 0) {
            if (--depth == 0 && objectStart != std::string::npos) {
                const std::string object =
                    json.substr(objectStart, position - objectStart + 1);
                CatalogItem item;
                if (findStringField(object, "id", item.id) &&
                    findStringField(object, "name", item.name)) {
                    findStringField(object, "poster", item.poster);
                    items.push_back(item);
                }
                objectStart = std::string::npos;
            }
        } else if (current == ']' && depth == 0) {
            return !items.empty();
        }
    }
    return !items.empty();
}

bool parseMetaDetails(const std::string& json, MetaDetails& details) {
    details = {};
    const size_t meta = json.find("\"meta\"");
    if (meta == std::string::npos) return false;
    const size_t objectStart = json.find('{', meta);
    if (objectStart == std::string::npos) return false;

    bool inString = false;
    bool escaped = false;
    int depth = 0;
    size_t objectEnd = std::string::npos;
    for (size_t position = objectStart; position < json.size(); ++position) {
        const char current = json[position];
        if (inString) {
            if (escaped) escaped = false;
            else if (current == '\\') escaped = true;
            else if (current == '"') inString = false;
            continue;
        }
        if (current == '"') inString = true;
        else if (current == '{') ++depth;
        else if (current == '}' && --depth == 0) {
            objectEnd = position;
            break;
        }
    }
    if (objectEnd == std::string::npos) return false;
    const std::string object =
        json.substr(objectStart, objectEnd - objectStart + 1);
    if (!findStringField(object, "id", details.id) ||
        !findStringField(object, "name", details.name)) return false;
    findStringField(object, "description", details.description);
    findStringField(object, "releaseInfo", details.releaseInfo);
    findStringField(object, "runtime", details.runtime);
    return true;
}

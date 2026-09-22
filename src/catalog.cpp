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

bool findIntField(const std::string& object, const char* field, int& value) {
    const std::string key = std::string("\"") + field + "\"";
    size_t position = object.find(key);
    if (position == std::string::npos) return false;
    position = object.find(':', position + key.size());
    if (position == std::string::npos) return false;
    do { ++position; } while (position < object.size() &&
        std::isspace(static_cast<unsigned char>(object[position])));
    int parsed = 0;
    bool found = false;
    while (position < object.size() && std::isdigit(
            static_cast<unsigned char>(object[position]))) {
        found = true;
        parsed = parsed * 10 + object[position++] - '0';
    }
    if (found) value = parsed;
    return found;
}

bool findStringArrayField(
    const std::string& object, const char* field, std::string& joined) {
    const std::string key = std::string("\"") + field + "\"";
    size_t position = object.find(key);
    if (position == std::string::npos) return false;
    position = object.find('[', position + key.size());
    if (position == std::string::npos) return false;
    joined.clear();
    for (++position; position < object.size(); ++position) {
        while (position < object.size() && object[position] != '"' &&
               object[position] != ']') ++position;
        if (position >= object.size() || object[position] == ']') break;
        std::string value;
        size_t end = 0;
        if (!readJsonString(object, position, value, end)) return false;
        if (!joined.empty()) joined += " / ";
        joined += value;
        position = end - 1;
    }
    return !joined.empty();
}

void parseEpisodes(const std::string& object, std::vector<MetaDetails::Episode>& episodes) {
    const size_t videos = object.find("\"videos\"");
    if (videos == std::string::npos) return;
    size_t position = object.find('[', videos);
    if (position == std::string::npos) return;
    bool inString = false;
    bool escaped = false;
    int depth = 0;
    size_t start = std::string::npos;
    for (++position; position < object.size() && episodes.size() < 256; ++position) {
        const char current = object[position];
        if (inString) {
            if (escaped) escaped = false;
            else if (current == '\\') escaped = true;
            else if (current == '"') inString = false;
            continue;
        }
        if (current == '"') inString = true;
        else if (current == '{') {
            if (depth++ == 0) start = position;
        } else if (current == '}' && depth > 0 && --depth == 0) {
            const std::string video = object.substr(start, position - start + 1);
            MetaDetails::Episode episode;
            if (findStringField(video, "id", episode.id) &&
                findIntField(video, "season", episode.season) &&
                findIntField(video, "episode", episode.episode)) {
                findStringField(video, "title", episode.title);
                episodes.push_back(episode);
            }
        } else if (current == ']' && depth == 0) break;
    }
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
    findStringField(object, "imdbRating", details.imdbRating);
    findStringArrayField(object, "genres", details.genres);
    parseEpisodes(object, details.episodes);
    return true;
}

bool parseStreamItems(
    const std::string& json,
    std::vector<StreamItem>& streams,
    size_t maximumItems) {
    streams.clear();
    const size_t marker = json.find("\"streams\"");
    if (marker == std::string::npos) return false;
    size_t position = json.find('[', marker);
    if (position == std::string::npos) return false;
    bool inString = false;
    bool escaped = false;
    int depth = 0;
    size_t start = std::string::npos;
    for (++position; position < json.size() && streams.size() < maximumItems;
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
            if (depth++ == 0) start = position;
        } else if (current == '}' && depth > 0 && --depth == 0) {
            const std::string object = json.substr(start, position - start + 1);
            StreamItem stream;
            findStringField(object, "name", stream.name);
            findStringField(object, "title", stream.title);
            findStringField(object, "url", stream.url);
            findStringField(object, "infoHash", stream.infoHash);
            findIntField(object, "fileIdx", stream.fileIndex);
            if (!stream.url.empty() || !stream.infoHash.empty()) {
                streams.push_back(stream);
            }
        } else if (current == ']' && depth == 0) break;
    }
    return !streams.empty();
}

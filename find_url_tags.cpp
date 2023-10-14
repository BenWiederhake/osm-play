#include <algorithm> // sort
#include <cassert>
#include <cstdio>
#include <cstring> // strcmp
#include <functional> // greater
#include <string>
#include <unordered_set>
#include <vector>

#include <osmium/io/pbf_input.hpp>
#include <osmium/io/reader_with_progress_bar.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
//#include <osmium/relations/relations_manager.hpp>

//#include "relation_list.hpp"

static const char* const INPUT_FILENAME = "/scratch/osm/bochum_6.99890,51.38677,7.39913,51.58303_231002.osm.pbf";
// static const char* const INPUT_FILENAME = "/scratch/osm/europe-latest.osm.pbf";
// static const char* const INPUT_FILENAME = "/scratch/osm/planet-231002.osm.pbf";

static const char* const OUTPUT_FILENAME = "/scratch/osm/tags_used_for_urls.lst";

// Detect url-like tags by looking for https-links:
static const char* const STRING_IN_EVERY_URL = "https://";
static const size_t STRING_IN_EVERY_URL_LEN = strlen(STRING_IN_EVERY_URL);

class FindUrlHandler : public osmium::handler::Handler {
public:
    void any_object(osmium::OSMObject const& obj) {
        for (auto const& tag : obj.tags()) {
            if (0 != strncmp(tag.value(), STRING_IN_EVERY_URL, STRING_IN_EVERY_URL_LEN)) {
                // Doesn't start with "https://".
                continue;
            }
            auto it_and_bool = tags_used.emplace(tag.key(), 1);
            if (!it_and_bool.second) {
                // Nothing was emplaced, i.e. the entry already existed.
                // Therefore, we want to increase the count:
                it_and_bool.first->second += 1;
            }
        }
    }

    void way(const osmium::Way& way) {
        any_object(way);
    }

    void node(const osmium::Node& node) {
        any_object(node);
    }

    void relation(const osmium::Relation& relation) {
        any_object(relation);
    }

    std::unordered_map<std::string, size_t> tags_used {};
};

int main() {
    printf("Reading %s …\n", INPUT_FILENAME);
    printf("(Using strlen(\"%s\") = %lu)\n", STRING_IN_EVERY_URL, STRING_IN_EVERY_URL_LEN);
    osmium::io::ReaderWithProgressBar reader{true, INPUT_FILENAME, osmium::osm_entity_bits::all};
    FindUrlHandler handler;
    osmium::apply(reader, handler);
    reader.close();

    printf("Done reading, found %lu tags. Sorting by match count …\n", handler.tags_used.size());
    std::vector<std::pair<size_t, std::string>> counts;
    for (auto it = handler.tags_used.begin(); it != handler.tags_used.end(); it = handler.tags_used.erase(it)) {
        counts.push_back({it->second, std::move(it->first)});
    }
    std::sort(counts.begin(), counts.end(), std::greater());

    printf("Done sorting. Writing to %s …\n", OUTPUT_FILENAME);
    FILE* fp = fopen(OUTPUT_FILENAME, "w");
    assert(fp != nullptr);
    for (auto const& item : counts) {
        fprintf(fp, "%lu\t%s\n", item.first, item.second.c_str());
    }
    fclose(fp);

    printf("All done!\n");
    return 0;
}

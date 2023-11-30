#include <cassert>
#include <cstdio>

#include <osmium/io/pbf_input.hpp>
#include <osmium/io/reader_with_progress_bar.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>

static const char* const INPUT_FILENAME = "/scratch/osm/bochum_6.99890,51.38677,7.39913,51.58303_231002.osm.pbf";
//static const char* const INPUT_FILENAME = "/scratch/osm/europe-latest.osm.pbf";

static const size_t KEEP_LARGEST_ITEMS_NUM = 5000;
static const char* const OUTPUT_FILENAME = "/scratch/osm/tag-count-histogram.csv";

class ItemSizeEntry {
public:
    ItemSizeEntry(osmium::OSMObject const& obj)
        : m_item_type(obj.type())
        , m_id(obj.id())
    {
        for ([[maybe_unused]] auto const& tag : obj.tags()) {
            m_tag_data_size += 1 + strlen(tag.key()) + 1 + strlen(tag.value());
        }
        // TODO: Missing constant term, but that's not really relevant to determine the top percentile.
        // FIXME: Size of relation membership list.
    }

    bool operator<(ItemSizeEntry const& other) const {
        if (other.m_tag_data_size != m_tag_data_size)
            return other.m_tag_data_size < m_tag_data_size;
        if (other.m_item_type != m_item_type)
            return other.m_item_type < m_item_type;
        if (other.m_id != m_id)
            return other.m_id < m_id;
        return false;
    }

    size_t m_tag_data_size {0};
    osmium::item_type m_item_type;
    osmium::object_id_type m_id;
};

class StatsHandler : public osmium::handler::Handler {
public:
    void any_object(osmium::OSMObject const& obj) {
        m_items.push_back(ItemSizeEntry{obj});
        if (m_items.size() > 10 * KEEP_LARGEST_ITEMS_NUM) {
            prune();
        }
    }

    void node(const osmium::Node& node) {
        m_nodes += 1;
        any_object(node);
    }

    void way(const osmium::Way& way) {
        m_ways += 1;
        any_object(way);
    }

    void relation(const osmium::Relation& relation) {
        m_relations += 1;
        any_object(relation);
    }

    void prune() {
        std::sort(m_items.begin(), m_items.end());
        m_items.erase(m_items.begin() + KEEP_LARGEST_ITEMS_NUM, m_items.end());
    }

    size_t m_nodes {0};
    size_t m_ways {0};
    size_t m_relations {0};
    std::vector<ItemSizeEntry> m_items {};
};

int main() {
    printf("Running on %s …\n", INPUT_FILENAME);
    StatsHandler stats_handler;
    {
        osmium::io::ReaderWithProgressBar reader{true, INPUT_FILENAME, osmium::osm_entity_bits::all};
        osmium::apply(reader, stats_handler);
        reader.close();
    }
    stats_handler.prune();

    printf("Done counting. Stats:\n");
    printf("  %lu nodes, %lu ways, %lu relations\n", stats_handler.m_nodes, stats_handler.m_ways, stats_handler.m_relations);
    size_t count_any = stats_handler.m_nodes + stats_handler.m_ways + stats_handler.m_relations;
    printf("Collected stats on the largest %lu items (%f %% of the database). Writing to %s …\n", stats_handler.m_items.size(), stats_handler.m_items.size() * 100.0 / count_any, OUTPUT_FILENAME);
    FILE* fp = fopen(OUTPUT_FILENAME, "w");
    assert(nullptr != fp);
    for (auto const& entry : stats_handler.m_items) {
        fprintf(fp, "%c%lu,%lu\n", osmium::item_type_to_char(entry.m_item_type), entry.m_id, entry.m_tag_data_size);
    }
    fclose(fp);

    printf("All done!\n");
    return 0;
}

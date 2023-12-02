#include <cassert>
#include <cstdio>

#include <osmium/io/pbf_input.hpp>
#include <osmium/io/pbf_input_randomaccess.hpp>
#include <osmium/io/reader.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>

static const char* const INPUT_FILENAME = "/scratch/osm/planet-231002.osm.pbf"; // 72 GiB, >600 million ways, guessing around 1134 million ways
// Out of 1134 million objects, want to capture roughly 550. That means 1 in 2 000 000. Choose closest prime for fun.
static const osmium::object_id_type ANALYZE_WAY_MODULO = 2'000'003;

// static const char* const INPUT_FILENAME = "/scratch/osm/germany-latest_20231101.osm.pbf"; // 4.0 GiB, 63 million ways
// // Out of 63 million objects, want to capture roughly 600. That means 1 in 100 000. Choose closest prime for fun.
// static const osmium::object_id_type ANALYZE_WAY_MODULO = 100'003;

static bool is_selected(osmium::object_id_type id) {
    return id % ANALYZE_WAY_MODULO == 0;
}

class RareObjectLocator : public osmium::handler::Handler {
public:
    explicit RareObjectLocator(osmium::io::PbfBlockIndexTable& table)
        : m_table(table)
    {
    }

    void way(const osmium::Way& way) {
        if (!is_selected(way.id()))
            return;
        osmium::Location loc = resolve_way(way);
        printf("w%lu x%d y%d\n", way.id(), loc.x(), loc.y());
    }

    //void relation(const osmium::Relation& relation) {
    //    if (!is_selected(relation.id))
    //        return;
    //    osmium::location loc = resolve_relation(relation);
    //    printf("r%lu x%d y%d\n", relation.id(), loc.x(), loc.y());
    //}

private:
    osmium::Location resolve_way(const osmium::Way& way) {
        for (auto const& noderef : way.nodes()) {
            osmium::Location loc = resolve_node_id(noderef.ref());
            if (loc)
                return loc;
        }
        return osmium::Location();
    }

    osmium::Location resolve_node_id(const osmium::object_id_type node_id) {
        auto buffers = m_table.binary_search_object(osmium::item_type::node, node_id, osmium::io::read_meta::no);
        for (auto buf_it = buffers.rbegin(); buf_it != buffers.rend(); ++buf_it) {
            const auto& buffer = *buf_it;
            for (auto it = buffer->begin<osmium::OSMObject>(); it != buffer->end<osmium::OSMObject>(); ++it) {
                if (it->type() != osmium::item_type::node) {
                    // Exploit the fact that nodes come first, so we immediately know that we're behind where the needle would have been.
                    return osmium::Location();
                }
                if (it->id() > node_id) {
                    // Exploit the fact that early IDs come first, so we immediately know that we're behind where the needle would have been.
                    return osmium::Location();
                }
                if (it->id() == node_id) {
                    return static_cast<osmium::Node&>(*it).location();
                }
            }
        }
        return osmium::Location();
    }

    osmium::io::PbfBlockIndexTable& m_table;
};

int main() {
    printf("# Running on %s …\n", INPUT_FILENAME);
    osmium::io::PbfBlockIndexTable table {INPUT_FILENAME};
    printf("# File has %lu blocks.\n", table.block_starts().size());
    RareObjectLocator rare_object_locator {table};
    osmium::io::Reader reader{INPUT_FILENAME, osmium::osm_entity_bits::way};
    osmium::apply(reader, rare_object_locator);
    reader.close();

    printf("# Done iterating.\n");
    return 0;
}

// $ OSMIUM_CLEAN_PAGE_CACHE_AFTER_READ=no hyperfine ./extract_some_ways_random_access  # germany
// Benchmark 1: ./extract_some_ways
//   Time (mean ± σ):      6.719 s ±  0.027 s    [User: 65.261 s, System: 2.291 s]
//   Range (min … max):    6.682 s …  6.774 s    10 runs

// $ cachedel /scratch/osm/planet-231002.osm.pbf && /usr/bin/time ./extract_some_ways_random_access > ways_ra.lst  # planet
// 1130.08user 60.17system 2:49.52elapsed 702%CPU (0avgtext+0avgdata 738416maxresident)k
// 160011136inputs+32outputs (0major+21547502minor)pagefaults 0swaps

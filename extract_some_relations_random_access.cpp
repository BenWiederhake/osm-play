#include <cassert>
#include <cstdio>

#include <osmium/io/pbf_input.hpp>
#include <osmium/io/pbf_input_randomaccess.hpp>
#include <osmium/io/reader.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>

static const char* const INPUT_FILENAME = "/scratch/osm/planet-231002.osm.pbf"; // 72 GiB, 11 million relations
// Out of 11 million relations, want to capture roughly 110. That means 1 in 100 000. Choose closest prime for fun.
static const osmium::object_id_type ANALYZE_WAY_MODULO = 100'003;

static bool is_selected(osmium::object_id_type id) {
    return id % ANALYZE_WAY_MODULO == 0;
}

class RareObjectLocator : public osmium::handler::Handler {
public:
    explicit RareObjectLocator(osmium::io::PbfBlockIndexTable& table)
        : m_table(table)
    {
    }

    void relation(const osmium::Relation& relation) {
        if (!is_selected(relation.id()))
            return;
        printf("# r%lu\n", relation.id());
        osmium::Location loc = resolve(relation);
        printf("r%lu x%d y%d\n", relation.id(), loc.x(), loc.y());
    }

private:
    osmium::Location resolve(const osmium::Relation& relation) {
        for (auto const& memberref : relation.members()) {
            osmium::Location loc = resolve_id(memberref.type(), memberref.ref());
            if (loc)
                return loc;
        }
        return osmium::Location();
    }

    osmium::Location resolve(const osmium::Way& way) {
        for (auto const& noderef : way.nodes()) {
            osmium::Location loc = resolve_id(osmium::item_type::node, noderef.ref());
            if (loc)
                return loc;
        }
        return osmium::Location();
    }

    osmium::Location resolve_id(osmium::item_type type, const osmium::object_id_type id) {
        printf("# -> %c%lu\n", osmium::item_type_to_char(type), id);
        auto buffers = m_table.binary_search_object(type, id, osmium::io::read_meta::no);
        for (auto buf_it = buffers.rbegin(); buf_it != buffers.rend(); ++buf_it) {
            const auto& buffer = *buf_it;
            for (auto it = buffer->begin<osmium::OSMObject>(); it != buffer->end<osmium::OSMObject>(); ++it) {
                if (it->type() != osmium::item_type::node) {
                    // TODO: Exploit order on object types?
                    continue;
                }
                if (it->id() > id) {
                    // Exploit the fact that early IDs come first, so we immediately know that we're behind where the needle would have been.
                    return osmium::Location();
                }
                if (it->id() == id) {
                    switch (type) {
                    case osmium::item_type::node:
                        return static_cast<osmium::Node&>(*it).location();
                    case osmium::item_type::way:
                        return resolve(static_cast<osmium::Way&>(*it));
                    case osmium::item_type::relation:
                        return resolve(static_cast<osmium::Relation&>(*it));
                    default:
                        printf("Object %c%lu has weird item type %u?!\n", osmium::item_type_to_char(type), id, static_cast<uint16_t>(type));
                        return osmium::Location();
                    }
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
    osmium::io::Reader reader{INPUT_FILENAME, osmium::osm_entity_bits::relation};
    osmium::apply(reader, rare_object_locator);
    reader.close();

    printf("# Done iterating.\n");
    return 0;
}

// $ OSMIUM_CLEAN_PAGE_CACHE_AFTER_READ=no hyperfine ./extract_some_relations_random_access 
// Benchmark 1: ./extract_some_relations_random_access
//   Time (mean ± σ):     142.166 s ±  1.798 s    [User: 722.182 s, System: 38.273 s]
//   Range (min … max):   139.468 s … 145.196 s    10 runs

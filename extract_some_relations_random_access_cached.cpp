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
    explicit RareObjectLocator(osmium::io::CachedRandomAccessPbf& resolver)
        : m_resolver(resolver)
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
            osmium::Location loc;
            printf("# -> %c%lu\n", osmium::item_type_to_char(memberref.type()), memberref.ref());
            m_resolver.visit_object(
                memberref.type(),
                memberref.ref(),
                [&](osmium::OSMObject const& object){
                    loc = resolve(object);
                }
            );
            if (loc)
                return loc;
        }
        return osmium::Location();
    }

    osmium::Location resolve(const osmium::Way& way) {
        for (auto const& noderef : way.nodes()) {
            printf("# -> n%lu\n", noderef.ref());
            osmium::Location loc;
            m_resolver.visit_node(
                noderef.ref(),
                [&](osmium::Node const& node){
                    printf("# @ n%lu\n", node.id());
                    loc = node.location();
                }
            );
            if (loc)
                return loc;
        }
        return osmium::Location();
    }

    osmium::Location resolve(osmium::OSMObject const& object) {
        switch (object.type()) {
        case osmium::item_type::node:
            printf("# @ n%lu\n", object.id());
            return static_cast<osmium::Node const&>(object).location();
        case osmium::item_type::way:
            return resolve(static_cast<osmium::Way const&>(object));
        case osmium::item_type::relation:
            return resolve(static_cast<osmium::Relation const&>(object));
        default:
            printf("Object %c%lu has weird item type %u?!\n", osmium::item_type_to_char(object.type()), object.id(), static_cast<uint16_t>(object.type()));
            return osmium::Location();
        }
    }

    osmium::io::CachedRandomAccessPbf& m_resolver;
};

int main() {
    printf("# Running on %s …\n", INPUT_FILENAME);
    osmium::io::PbfBlockIndexTable table {INPUT_FILENAME};
    osmium::io::CachedRandomAccessPbf resolver {table};

    // for (size_t i = 1353; i < 1355 + 1; ++i) {
    //     osmium::OSMObject const& first_on_page = resolver.first_in_block(i);
    //     printf("Page %lu starts with object %c%lu\n", i, osmium::item_type_to_char(first_on_page.type()), first_on_page.id());
    // }
    // resolver.first_in_block(1353);
    // resolver.visit_node(
    //     469773053,
    //     [&](osmium::Node const& node){
    //         printf("# the node 469773053 exists!!!!\n");
    //     }
    // );
    // exit(42);

    printf("# File has %lu blocks.\n", table.block_starts().size());
    RareObjectLocator rare_object_locator {resolver};
    osmium::io::Reader reader{INPUT_FILENAME, osmium::osm_entity_bits::relation};
    osmium::apply(reader, rare_object_locator);
    reader.close();

    printf("# Done iterating.\n");
    return 0;
}

#include <cassert>
#include <cstdio>

#include <osmium/io/pbf_input.hpp>
#include <osmium/io/pbf_output.hpp>
#include <osmium/io/reader_with_progress_bar.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/relations/relations_manager.hpp>

#include "relation_list.hpp"

static const char* const INPUT_FILENAME = "/scratch/osm/detmold.osm.pbf";
static const char* const OUTPUT_FILENAME = "/scratch/osm/relevant_detmold.osm.pbf";

// Takes about 40 minutes.
// static const char* const INPUT_FILENAME = "/scratch/osm/planet-231002.osm.pbf";
// static const char* const OUTPUT_FILENAME = "/scratch/osm/relevant_planet-231002.osm.pbf";

class ExtractRelevantManager : public osmium::relations::RelationsManager<ExtractRelevantManager, false, true, true> {
public:
    bool new_relation(const osmium::Relation& relation) noexcept {
        return is_relevant_relation(relation.id());
    }

    void complete_relation(const osmium::Relation& relation) {
        this->maybe_complete_relation(relation);
    }

    void maybe_complete_relation(const osmium::Relation& relation) {
        for (const auto& member : relation.members()) {
            if (member.ref() == 0 || member.type() != osmium::item_type::way) {
                continue;
            }
            const auto* obj_ptr = way_db.get_object(member.ref());
            if (obj_ptr == nullptr) {
                printf("missing way %ld?!\n", member.ref());
                continue;
            }
            const auto* way_ptr = static_cast<const osmium::Way*>(obj_ptr);
            //for (auto& node_ref : way.nodes()) {
            //    if (!node_ref.location().valid()) {
            //        printf("Node %ld in way %ld has invalid location?!\n", node_ref.ref(), way.id());
            //        continue;
            //    }
            //}
            this->buffer().add_item(*obj_ptr);
        }
        this->buffer().add_item(relation);
        printf("Wrote relation %ld\n", relation.id());
        this->buffer().commit();
    }
};

int main() {
    printf("read input header\n");
    osmium::io::File input_file{INPUT_FILENAME};

    printf("set up manager\n");
    ExtractRelevantManager manager;
    osmium::io::Writer writer{OUTPUT_FILENAME, osmium::io::overwrite::allow};
    manager.set_callback([&](osmium::memory::Buffer&& buffer){
        writer(std::move(buffer));
    });

    // First pass through the file
    printf("read relations\n");
    osmium::relations::read_relations(input_file, manager);
    // Second pass through the file
    printf("prepare reading members\n");
    osmium::io::ReaderWithProgressBar reader{true, input_file};
    printf("actually read members\n");
    osmium::apply(reader, manager.handler());
    printf("handle incomplete relations\n");
    manager.for_each_incomplete_relation([&](const osmium::relations::RelationHandle& handle){
        manager.maybe_complete_relation(*handle);
    });
    printf("flush\n");
    manager.flush();
    writer(std::move(manager.read()));
    writer.flush();
    writer.close();
    printf("end main\n");

    return 0; // Implicit: Deconstruct manager, which finishes and closes the file.
}

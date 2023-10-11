#include <cassert>
#include <cstdio>

#include <osmium/io/pbf_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
// #include <osmium/geom/mercator_projection.hpp>
#include <osmium/relations/relations_manager.hpp>

#include "relation_list.hpp"

static const char* const INPUT_FILENAME = "/scratch/osm/detmold.osm.pbf";
// static const char* const INPUT_FILENAME = "/scratch/osm/relevant_planet-231002.osm.pbf";

static const char* const OUTPUT_FILENAME = "/scratch/osm/laendergrenzen.svg";
static const double MIN_LONG_DEG = 0.0;
static const double MAX_LONG_DEG = 24.0;
static const double MIN_LAT_DEG = 46.0;
static const double MAX_LAT_DEG = 68.0;

static const double PX_PER_LAT_DEG = 100.0;
// v3.y = sin(latitude);
// v3.x = cos(latitude) * sin(longitude);
// v3.z = cos(latitude) * cos(longitude);
// We want a square pixel to roughly represent a "square" area in real life.
// So if a single latitudinal degree covers PX_PER_LAT_DEG pixels,
// then a single longitudinal degree should cover roughly PX_PER_LAT_DEG * cos(latitude) pixels.
// We want to center on the middle of Germany, let's pick 51° N for that.
// cos(51°) is approximately 0.62932:
static const double PX_PER_LONG_DEG = PX_PER_LAT_DEG * 0.62932;

// Derived constants:
static const double WIDTH = (MAX_LONG_DEG - MIN_LONG_DEG) * PX_PER_LONG_DEG;
static const double HEIGHT = (MAX_LAT_DEG - MIN_LAT_DEG) * PX_PER_LAT_DEG;
// TODO: Plug in the values we had from the old map.

class SvgWriter {
public:
    explicit SvgWriter(const char* const output_filename)
        : m_in_relation(false)
    {
        // Note: iostream supports RAII, but it seems a better idea to use fprintf than streaming I/O.
        m_file = fopen(output_filename, "w");
        assert(m_file);
        fprintf(m_file, "<svg width=\"%f\" height=\"%f\">\n", WIDTH, HEIGHT);
        //    <rect width="300" height="100" style="fill:rgb(0,0,255);stroke-width:3;stroke:rgb(0,0,0)" />
        fprintf(m_file, "<rect width=\"%f\" height=\"%f\" style=\"fill:rgb(255,255,255)\"/>\n", WIDTH, HEIGHT);
    }
    SvgWriter(const SvgWriter&) = delete;
    SvgWriter(SvgWriter&&) = delete;
    SvgWriter& operator=(const SvgWriter&) = delete;
    SvgWriter& operator=(SvgWriter&&) = delete;

    ~SvgWriter() {
        assert(!m_in_relation);
        fputs("</svg>\n", m_file);
        fclose(m_file);
        m_file = nullptr;
    }

    void begin_relation(const osmium::Relation& relation) {
        assert(!m_in_relation);
        fprintf(m_file, "<g id=\"relation_%ld\">\n", relation.id());
        m_in_relation = true;
    }

    void add_way_to_relation(const osmium::Way& way) {
        assert(m_in_relation);
        fprintf(m_file, " <polyline id=\"way_%ld\" style=\"fill:none;stroke:black;stroke-width:1\" points=\"", way.id());
        for (auto& node_ref : way.nodes()) {
            if (!node_ref.location().valid()) {
                printf("Node %ld in way %ld has invalid location?!\n", node_ref.ref(), way.id());
                continue;
            }
            printf("GOOOOOD! Node %ld in way %ld has VALID location!\n", node_ref.ref(), way.id());
            exit(42);
            double x = (node_ref.lon() - MIN_LONG_DEG) * PX_PER_LONG_DEG;
            double y = (MIN_LAT_DEG - node_ref.lat()) * PX_PER_LAT_DEG;
            fprintf(m_file, " %f,%f", x, y);
        }
        fputs("\"/>\n", m_file);
    }

    void finish_relation() {
        assert(m_in_relation);
        fputs("</g>\n", m_file);
        m_in_relation = false;
    }

private:
    bool m_in_relation;
    FILE* m_file;
};

class FeedSvgManager : public osmium::relations::RelationsManager<FeedSvgManager, true, true, true, false> {
public:
    explicit FeedSvgManager(const char* const output_filename)
        : m_svg_writer(output_filename)
    {
    }

    bool new_relation(const osmium::Relation& relation) noexcept {
        return is_relevant_relation(relation.id());
    }

    void complete_relation(const osmium::Relation& relation) {
        this->maybe_complete_relation(relation);
    }

    void maybe_complete_relation(const osmium::Relation& relation) {
        m_svg_writer.begin_relation(relation);
        auto& way_db = this->member_database(osmium::item_type::way);
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
            m_svg_writer.add_way_to_relation(*way_ptr);
        }
        m_svg_writer.finish_relation();
    }

private:
    SvgWriter m_svg_writer;
};

int main() {
    printf("reading input header\n");
    osmium::io::File input_file{INPUT_FILENAME};

    // Instantiate manager class
    printf("setting up manager\n");
    FeedSvgManager manager{OUTPUT_FILENAME};

    // First pass through the file
    printf("reading relations\n");
    osmium::relations::read_relations(input_file, manager);
    // Second pass through the file
    printf("reading members\n");
    // No progress bar, since this should be very fast anyway
    osmium::io::Reader reader{input_file};
    osmium::apply(reader, manager.handler());
    printf("handling incomplete\n");
    manager.for_each_incomplete_relation([&](const osmium::relations::RelationHandle& handle){
        manager.maybe_complete_relation(*handle);
    });
    printf("end main\n");
    return 0; // Implicit: Deconstruct manager, which finishes and closes the file.
}

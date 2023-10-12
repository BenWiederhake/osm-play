#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <vector>

#include <osmium/io/pbf_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
// #include <osmium/geom/mercator_projection.hpp>
#include <osmium/relations/relations_manager.hpp>

#include "relation_list.hpp"

static const char* const INPUT_FILENAME = "/scratch/osm/relevant_europe-latest.osm.pbf";
//static const char* const INPUT_FILENAME = "/scratch/osm/relevant_planet-231002.osm.pbf";

static const char* const OUTPUT_FILENAME = "/scratch/osm/laendergrenzen.svg";
// Achieve compatibility with … a thing:
// ORIGIN	45.88919, 4.96126
// X0 Y130	55.67336, 4.96126
// X110 Y0	45.88919, 18.02294
// X110 Y130	55.67336, 18.02294
static const double MIN_LONG_DEG = 4.96126;
static const double MAX_LONG_DEG = 18.02294;
static const double MIN_LAT_DEG = 45.88919;
static const double MAX_LAT_DEG = 55.67336;
static const double PX_PER_LONG_DEG = 99.52777896870847;
static const double PX_PAINT_TRESHOLD_SQUARED = 0.5;

// v3.y = sin(latitude);
// v3.x = cos(latitude) * sin(longitude);
// v3.z = cos(latitude) * cos(longitude);
// We want a square pixel to roughly represent a "square" area in real life.
// So if a single latitudinal degree covers PX_PER_LAT_DEG pixels,
// then a single longitudinal degree should cover roughly PX_PER_LAT_DEG * cos(latitude) pixels.
// We want to center on the middle of Germany, let's pick 50.3° N for that.
// cos(50.3°) is approximately 0.6387678:
static const double PX_PER_LAT_DEG = PX_PER_LONG_DEG / 0.6387678;

// Derived constants:
static const double WIDTH = (MAX_LONG_DEG - MIN_LONG_DEG) * PX_PER_LONG_DEG;
static const double HEIGHT = (MAX_LAT_DEG - MIN_LAT_DEG) * PX_PER_LAT_DEG;
// TODO: Plug in the values we had from the old map.

static_assert(sizeof(osmium::Location) == 8);

class ExtractRelevantHandler : public osmium::handler::Handler {
public:
    void node(const osmium::Node& node) {
        assert(node_to_location.find(node.id()) == node_to_location.end());
        node_to_location.insert({node.id(), node.location()});
    }

    void way(const osmium::Way& way) {
        assert(way_to_nodes.find(way.id()) == way_to_nodes.end());
        std::vector<osmium::object_id_type> node_ids;
        for (auto& node_ref : way.nodes()) {
            node_ids.push_back(node_ref.ref());
        }
        way_to_nodes.insert({way.id(), node_ids});
    }

    void relation(const osmium::Relation& relation) {
        assert(relation_to_ways.find(relation.id()) == relation_to_ways.end());
        if (!is_relevant_relation(relation.id())) {
            this->discarded += 1;
            return;
        }
        std::vector<osmium::object_id_type> way_ids;
        for (auto& item_ref : relation.members()) {
            if (item_ref.type() == osmium::item_type::way) {
                way_ids.push_back(item_ref.ref());
            }
        }
        relation_to_ways.insert({relation.id(), way_ids});
    }

    size_t discarded = 0;
    std::map<osmium::object_id_type, osmium::Location> node_to_location;
    std::map<osmium::object_id_type, std::vector<osmium::object_id_type>> way_to_nodes;
    std::map<osmium::object_id_type, std::vector<osmium::object_id_type>> relation_to_ways;
};

class SvgWriter {
public:
    explicit SvgWriter(const char* const output_filename)
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
        fputs("</svg>\n", m_file);
        fclose(m_file);
        m_file = nullptr;
    }

    void write_relations_from(ExtractRelevantHandler const& handler) {
        for (auto& relation_entry : handler.relation_to_ways) {
            this->write_relation_from(relation_entry.first, relation_entry.second, handler);
        }
    }

    void write_relation_from(osmium::object_id_type relation_id, std::vector<osmium::object_id_type> const& ways_in_relation, ExtractRelevantHandler const& handler) {
        fprintf(m_file, "<g id=\"relation_%ld\">\n", relation_id);
        std::vector<osmium::object_id_type> consecutive_ways;
        for (auto way_id : ways_in_relation) {
            if (!consecutive_ways.empty()) {
                auto previous_way_id = consecutive_ways.back();
                auto previous_way_last_node = handler.way_to_nodes.at(previous_way_id).back();
                auto this_way_first_node = handler.way_to_nodes.at(way_id).front();
                bool is_consecutive = previous_way_last_node == this_way_first_node;
                // Note: This only detects about *half* of all consecutive ways!
                // This is because some ways might be in reversed order.
                // In fact it is even less, because the relation does not necessarily order the ways in a useful manner :(
                if (!is_consecutive) {
                    this->write_consecutive_ways_from(consecutive_ways, handler);
                    consecutive_ways.clear();
                }
            }
            consecutive_ways.push_back(way_id);
        }
        if (!consecutive_ways.empty()) {
            this->write_consecutive_ways_from(consecutive_ways, handler);
        }
        fputs("</g>\n", m_file);
    }

    void write_consecutive_ways_from(std::vector<osmium::object_id_type> way_ids, ExtractRelevantHandler const& handler) {
        fprintf(m_file, " <polyline id=\"ways");
        for (auto way_id : way_ids) {
            fprintf(m_file, "_%ld", way_id);
        }
        fprintf(m_file, "\" style=\"fill:none;stroke:rgb(%d,%d,%d);stroke-width:1\" points=\"", rand() % 256, rand() % 256, rand() % 256);
        for (auto way_id : way_ids) {
            for (auto& node_id : handler.way_to_nodes.at(way_id)) {
                // Note: On consecutive ways, the first node is duplicated.
                // However, this is automatically thrown out by skipping nearby nodes.
                auto const& location = handler.node_to_location.at(node_id);
                // The first and last locations *must* be written (to make extra-sure that loops are closed),
                // but intermediate points may be skipped.
                this->offer_location(location);
            }
        }
        this->flush_location();
        fputs("\"/>\n", m_file);
    }

    size_t skipped_painting() const {
        return m_skipped_painting;
    }

    size_t painted() const {
        return m_painted;
    }

private:
    void offer_location(osmium::Location const& location) {
        double x = (location.lon() - MIN_LONG_DEG) * PX_PER_LONG_DEG;
        double y = (MAX_LAT_DEG - location.lat()) * PX_PER_LAT_DEG;

        // Paint, unless the last painted point is close enough.
        bool should_update = true;
        if (m_last_painted_valid) {
            double dx = x - m_last_painted_x;
            double dy = y - m_last_painted_y;
            double dist_sq = dx * dx + dy * dy;
            should_update = dist_sq >= PX_PAINT_TRESHOLD_SQUARED;
        }

        if (should_update) {
            fprintf(m_file, " %f,%f", x, y);
            m_last_painted_x = x;
            m_last_painted_y = y;
            m_last_painted_valid = true;
            m_buffered_x = x;
            m_buffered_y = y;
            m_buffer_needs_painting = false;
            m_painted += 1;
        } else {
            m_buffered_x = x;
            m_buffered_y = y;
            m_buffer_needs_painting = true;
            m_skipped_painting += 1;
        }
    }

    void flush_location() {
        if (m_buffer_needs_painting) {
            fprintf(m_file, " %f,%f", m_buffered_x, m_buffered_y);
            m_painted += 1;
            m_skipped_painting -= 1;
        }
        m_last_painted_valid = false;
        m_buffer_needs_painting = false;
    }

    FILE* m_file;
    double m_last_painted_x;
    double m_last_painted_y;
    bool m_last_painted_valid {false};
    double m_buffered_x;
    double m_buffered_y;
    bool m_buffer_needs_painting {false};
    size_t m_skipped_painting {0};
    size_t m_painted {0};
};

int main() {
    printf("reading input header\n");
    srand(time(nullptr));
    osmium::io::Reader reader{INPUT_FILENAME, osmium::osm_entity_bits::all};
    ExtractRelevantHandler handler;

    printf("reading *all* data to memory (this assumes that you already ran 'osmium getid')\n");
    osmium::apply(reader, handler);
    reader.close();
    printf(
        "    got %ld nodes, %ld ways, %ld of %ld useful relations, and %ld useless relations\n",
        handler.node_to_location.size(),
        handler.way_to_nodes.size(),
        handler.relation_to_ways.size(),
        sizeof(EXPORT_RELATIONS) / sizeof(EXPORT_RELATIONS[0]),
        handler.discarded
    );

    printf("writing svg\n");
    SvgWriter writer{OUTPUT_FILENAME};
    writer.write_relations_from(handler);
    printf("   painted %lu nodes\n", writer.painted());
    printf("   could skip painting %lu nodes\n", writer.skipped_painting());

    printf("closing\n");
    return 0; // Implicit: Deconstruct manager, which finishes and closes the file.
}

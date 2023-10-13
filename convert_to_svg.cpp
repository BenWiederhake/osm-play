#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <set>
#include <vector>

#include <boost/range/adaptor/reversed.hpp>

#include <osmium/io/pbf_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
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
// 0.5 is reasonable. Set to -1.0 to disable (0.0 should probably also work).
static const double PX_PAINT_TRESHOLD_SQUARED = 0.81;
static const bool VERBOSE_SVG = false;

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
static_assert(sizeof(osmium::object_id_type) == 8);

static osmium::object_id_type abs_id(osmium::object_id_type id) {
    assert(id != 0);
    if (id < 0) {
        return -id;
    } else {
        return id;
    }
}


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
        assert(way.id() > 0);
        end_node_to_incident_ways.insert({node_ids.front(), way.id()});
        end_node_to_incident_ways.insert({node_ids.back(), -way.id()});
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

    void check() const {
        for (auto const& entry : end_node_to_incident_ways) {
            auto const& way_nodes = way_to_nodes.at(abs_id(entry.second));
            if (entry.second < 0) {
                assert(entry.first == way_nodes.back());
            } else {
                assert(entry.first == way_nodes.front());
            }
        }
    }

    osmium::Box compute_bbox(std::vector<osmium::object_id_type> const& consecutive_ways) const {
        osmium::Box bbox;
        for (auto signed_way_id : consecutive_ways) {
            auto const& way_nodes = way_to_nodes.at(abs_id(signed_way_id));
            for (auto node_id : way_nodes) {
                bbox.extend(node_to_location.at(node_id));
            }
        }
        return bbox;
    }

    size_t discarded = 0;
    std::map<osmium::object_id_type, osmium::Location> node_to_location;
    std::map<osmium::object_id_type, std::vector<osmium::object_id_type>> way_to_nodes;
    std::map<osmium::object_id_type, std::vector<osmium::object_id_type>> relation_to_ways;
    std::multimap<osmium::object_id_type, osmium::object_id_type> end_node_to_incident_ways;
};

class SvgWriter {
public:
    explicit SvgWriter(const char* const output_filename)
    {
        // Note: iostream supports RAII, but it seems a better idea to use fprintf than streaming I/O.
        m_file = fopen(output_filename, "w");
        assert(m_file);
        fprintf(m_file, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%f\" height=\"%f\">\n", WIDTH, HEIGHT);
        fprintf(m_file, " <rect width=\"%f\" height=\"%f\" style=\"fill:rgb(245,245,245)\"/>\n", WIDTH, HEIGHT);
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
        for (auto relation_id : EXPORT_RELATIONS) {
            this->write_relation_from(relation_id, handler);
        }
    }

    void write_relation_from(osmium::object_id_type relation_id, ExtractRelevantHandler const& handler) {
        std::vector<osmium::object_id_type> const& ways_in_relation = handler.relation_to_ways.at(relation_id);
        std::set<osmium::object_id_type> remaining_ways{ways_in_relation.begin(), ways_in_relation.end()};
        assert(remaining_ways.size() == ways_in_relation.size());
        std::vector<std::vector<osmium::object_id_type>> rings;
        while (!remaining_ways.empty()) {
            std::vector<osmium::object_id_type> consecutive_ways;
            osmium::object_id_type first_node;
            osmium::object_id_type last_node;
            {
                auto way_id = *remaining_ways.begin();
                remaining_ways.erase(remaining_ways.begin());
                auto const& way_nodes = handler.way_to_nodes.at(way_id);
                first_node = way_nodes.front();
                last_node = way_nodes.back();
                consecutive_ways.push_back(way_id);
            }
            // Try to find more ways of this relation that connect nicely, unless we formed a loop.
            // Note: This does not detect all cycles! But I hope this is enough.
            while (first_node != last_node) {
                bool found_usable_way = false;
                auto range = handler.end_node_to_incident_ways.equal_range(last_node);
                for (auto incident_way_iter = range.first; incident_way_iter != range.second; ++incident_way_iter) {
                    auto incident_way_id = incident_way_iter->second;
                    auto way_iter = remaining_ways.find(abs_id(incident_way_id));
                    if (way_iter == remaining_ways.end()) {
                        // The way is incident, yes, but since it's not part of this relation we need to skip it.
                        continue;
                    }
                    // We can use this!
                    found_usable_way = true;
                    remaining_ways.erase(way_iter);
                    consecutive_ways.push_back(incident_way_id);
                    auto const& way_nodes = handler.way_to_nodes.at(abs_id(incident_way_id));
                    if (incident_way_id > 0) {
                        assert(last_node == way_nodes.front());
                        last_node = way_nodes.back();
                    } else {
                        assert(last_node == way_nodes.back());
                        last_node = way_nodes.front();
                    }
                    // Because last_node (probably) changed, we have to search from scratch:
                    break;
                }
                if (!found_usable_way) {
                    // This is a dead end, we have to stop looking for ways to extend this way.
                    break;
                }
                // … otherwise, we could extend the way a little further, so loop again.
            }
            if (first_node != last_node) {
                printf("Cannot close ring in relation %lu involving ways %ld --(%ld)--> … --(%ld)--> %ld!\n",
                    relation_id, first_node, consecutive_ways.front(), consecutive_ways.back(), last_node);
                exit(1);
            }
            // We're done with the current ring!
            // Check whether it is even visible:
            auto bbox = handler.compute_bbox(consecutive_ways);
            double width_px = (bbox.right() - bbox.left()) * PX_PER_LONG_DEG;
            double height_px = (bbox.top() - bbox.bottom()) * PX_PER_LAT_DEG;
            if (width_px < 1.0 || height_px < 1.0) {
                // Skipping this ring entirely!
                printf("   Skipping ring with %lu ways (e.g. %lu) in relation %lu: bbox is only %f x %f pixels.\n",
                    consecutive_ways.size(), consecutive_ways.front(), relation_id, width_px, height_px);
                continue;
            }
            rings.emplace_back(consecutive_ways);
        }
        this->write_rings_from(relation_id, rings, handler);
    }

    void write_rings_from(osmium::object_id_type relation_id, std::vector<std::vector<osmium::object_id_type>> const& rings, ExtractRelevantHandler const& handler) {
        if (VERBOSE_SVG) {
            fprintf(m_file, " <path id=\"relation_%ld_with_%lu_rings\"", relation_id, rings.size());
            fprintf(m_file, " comment=\"");
            for (auto const& ring : rings) {
                fprintf(m_file, "w%lu+%lumore,", ring.front(), ring.size() - 1);
            }
            fprintf(m_file, "\"");
        } else {
            fprintf(m_file, " <path");
        }
        fprintf(m_file, " stroke=\"rgb(245,245,245)\"");
        if (is_thick_stroke_relation(relation_id)) {
            fprintf(m_file, " stroke-width=\"5\"");
            fprintf(m_file, " fill=\"none\"");
        } else {
            fprintf(m_file, " stroke-width=\"1\"");
            fprintf(m_file, " fill-rule=\"evenodd\"");
            //fprintf(m_file, " fill=\"rgb(%d,%d,%d)\"", rand() % 256, rand() % 256, rand() % 256);
            fprintf(m_file, " fill=\"rgb(159,159,159)\"");
        }
        fprintf(m_file, " d=\"");
        for (auto const& ring : rings) {
            for (auto signed_way_id : ring) {
                auto const& way_nodes = handler.way_to_nodes.at(abs_id(signed_way_id));
                // Note: On consecutive ways, some nodes are duplicated.
                // However, this is automatically thrown out by skipping nearby nodes.
                if (signed_way_id < 0) {
                    for (auto& node_id : boost::adaptors::reverse(way_nodes)) {
                        this->offer_location(handler.node_to_location.at(node_id));
                    }
                } else {
                    for (auto& node_id : way_nodes) {
                        this->offer_location(handler.node_to_location.at(node_id));
                    }
                }
            }
            // The first and last locations *must* be written (to make extra-sure that loops are closed),
            // but intermediate points may be skipped.
            this->flush_location();
        }
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
            paint_location_now(x, y);
            m_buffered_x = x;
            m_buffered_y = y;
            m_buffer_needs_painting = false;
        } else {
            m_buffered_x = x;
            m_buffered_y = y;
            m_buffer_needs_painting = true;
            m_skipped_painting += 1;
        }
    }

    void flush_location() {
        if (m_buffer_needs_painting) {
            paint_location_now(m_buffered_x, m_buffered_y);
            m_skipped_painting -= 1;
        }
        m_last_painted_valid = false;
        m_buffer_needs_painting = false;
    }

    void paint_location_now(double x, double y) {
        fprintf(m_file, "%s%.1f,%.1f", m_last_painted_valid ? "L" : "M", x, y);
        m_last_painted_x = x;
        m_last_painted_y = y;
        m_last_painted_valid = true;
        m_painted += 1;
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
    printf("checking consistency …\n");
    handler.check();

    printf("writing svg\n");
    SvgWriter writer{OUTPUT_FILENAME};
    writer.write_relations_from(handler);
    printf("   painted %lu nodes\n", writer.painted());
    printf("   could skip painting %lu nodes\n", writer.skipped_painting());

    printf("closing\n");
    return 0; // Implicit: Deconstruct manager, which finishes and closes the file.
}

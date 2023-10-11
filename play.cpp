#include <cassert>
#include <cstdio>

#include <osmium/io/pbf_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/geom/mercator_projection.hpp>
#include <osmium/relations/relations_manager.hpp>

static const char* const INPUT_FILENAME = "/scratch/osm/detmold.osm.pbf";
// static const char* const INPUT_FILENAME = "/scratch/osm/planet-231002.osm.pbf";
static const char* const OUTPUT_FILENAME = "/scratch/osm/laendergrenzen.svg";
static const double MIN_LONG_DEG = 0.0;
static const double MAX_LONG_DEG = 24.0;
static const double MIN_LAT_DEG = 46.0;
static const double MAX_LAT_DEG = 68.0;

static const double PX_PER_LAT_DEG = 500.0;
// v3.y = sin(latitude);
// v3.x = cos(latitude) * sin(longitude);
// v3.z = cos(latitude) * cos(longitude);
// We want a square pixel to roughly represent a "square" area in real life.
// So if a single latitudinal degree covers PX_PER_LAT_DEG pixels,
// then a single longitudinal degree should cover roughly PX_PER_LAT_DEG * cos(latitude) pixels.
// We want to center on the middle of Germany, let's pick 51° N for that.
// cos(51°) is approximately 0.62932:
static const double PX_PER_LONG_DEG = PX_PER_LAT_DEG * 0.62932;

const osmium::object_id_type EXPORT_RELATIONS[] = {
    62781, // D Gesamt
    62611, // D BaWü
    2145268, // D Bay
    62422, // D Berlin
    62504, // D Brande
    62718, // D Bremen, Achtung Bremerhaven?
    451087, // D Hamburg
    62650, // D Hessen
    62774, // D Meckpom
    454192, // D Niedersachs
    62761, // D NRW
    62341, // D Rheinpfalz
    62372, // D Saarland
    62467, // D Sachsen
    62607, // D SachsAnhalt
    62775, // D Schles Hol
    62366, // D Thür
    16239, // AT Gesamt
    76909, // AT Burgenland
    52345, // AT Kärnten
    77189, // AT Niederöster
    102303, // AT Oberöster
    86539, // AT Salzburg
    35183, // AT Steiermark
    52343, // AT Tirol
    74942, // AT Vorarlberg
    109166, // AT Wien
};

// Derived constants:
static const double WIDTH = (MAX_LONG_DEG - MIN_LONG_DEG) * PX_PER_LONG_DEG;
static const double HEIGHT = (MAX_LAT_DEG - MIN_LAT_DEG) * PX_PER_LAT_DEG;
// TODO: Plug in the values we had from the old map.

class SvgWriter {
public:
    explicit SvgWriter(const char* const output_filename)
    {
        // Note: iostream supports RAII, but it seems a better idea to use fprintf than streaming I/O.
        m_file = fopen(output_filename, "w");
        assert(m_file);
        fputs("henlo\n", m_file);
    }
    SvgWriter(const SvgWriter&) = delete;
    SvgWriter(SvgWriter&&) = delete;
    SvgWriter& operator=(const SvgWriter&) = delete;
    SvgWriter& operator=(SvgWriter&&) = delete;

    ~SvgWriter() {
        fputs("final", m_file);
        fclose(m_file);
        m_file = nullptr;
    }

    void write_foo() {
        fprintf(m_file, "im %p\n", this);
    }

private:
    FILE* m_file;
};

class ExportBordersManager : public osmium::relations::RelationsManager<ExportBordersManager, true, true, true> {
public:
    explicit ExportBordersManager(const char* const output_filename)
        : m_svg_writer(output_filename)
    {
    }

    bool new_relation(const osmium::Relation& relation) noexcept {
        // TODO: Linear scan with 27 items … dunno if that's efficient or not.
        for (auto interesting_relation : EXPORT_RELATIONS) {
            if (interesting_relation == relation.id()) {
                return true;
            }
        }
        return false;
    }

    void complete_relation(const osmium::Relation& relation) {
        this->maybe_complete_relation(relation);
    }

    void maybe_complete_relation(const osmium::Relation& relation) {
        for (const auto& member : relation.members()) {
            if (member.ref() != 0) {
                const auto obj_ptr = this->member_database(member.type()).get_object(member.ref());
                if (obj_ptr == nullptr) {
                    printf("ref %ld points at no object?!\n", member.ref());
                } else {
                    printf("Holding object %ld\n", obj_ptr->id());
                }
            }
        }
    }

private:
    SvgWriter m_svg_writer;
};

int main() {
    printf("reading input header\n");
    osmium::io::File input_file{INPUT_FILENAME};

    // Instantiate manager class
    printf("setting up manager\n");
    ExportBordersManager manager{OUTPUT_FILENAME};

    // First pass through the file
    printf("reading relations\n");
    osmium::relations::read_relations(input_file, manager);
    // Second pass through the file
    printf("reading members\n");
    osmium::io::Reader reader{input_file};
    osmium::apply(reader, manager.handler());
    printf("handling incomplete\n");
    manager.for_each_incomplete_relation([&](const osmium::relations::RelationHandle& handle){
        manager.maybe_complete_relation(*handle);
    });
    printf("end main\n");
    return 0; // Implicit: Deconstruct manager, which finishes and closes the file.
}

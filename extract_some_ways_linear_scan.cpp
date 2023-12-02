#include <cassert>
#include <cstdio>
#include <unordered_set>

#include <osmium/io/pbf_input.hpp>
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

class WayEntry {
public:
    WayEntry(osmium::Way const& way)
        : m_way_id(way.id())
    {
        for (auto const& noderef : way.nodes()) {
            m_node_refs.push_back(noderef.ref());
        }
    }

    std::vector<osmium::object_id_type> const& nodes() const {
        return m_node_refs;
    }

    osmium::object_id_type way_id() const {
        return m_way_id;
    }

private:
    osmium::object_id_type m_way_id;
    std::vector<osmium::object_id_type> m_node_refs;
};

class WayNodesExtractor : public osmium::handler::Handler {
public:
    WayNodesExtractor() {
    }

    void way(const osmium::Way& way) {
        if (!is_selected(way.id()))
            return;
        m_way_entries.emplace_back(way);
    }

    //void relation(const osmium::Relation& relation) {
    //    ???
    //    Note that resolving relations this way will take at
    //    least three passes, and potentially dozens.
    //}

    std::vector<WayEntry> const& way_entries() const {
        return m_way_entries;
    }

    void clear() {
        m_way_entries.clear();
    }

private:
    std::vector<WayEntry> m_way_entries;
};

enum class NodeRelation {
    Before,
    Equal,
    After,
};

class InterestingNode {
public:
    InterestingNode(osmium::object_id_type node_id, osmium::object_id_type way_id)
        : m_node_id(node_id)
        , m_way_id(way_id)
    {
    }

    osmium::object_id_type node_id() const {
        return m_node_id;
    }

    osmium::object_id_type way_id() const {
        return m_way_id;
    }

    NodeRelation relative_to(osmium::object_id_type other_node_id) const {
        if (m_node_id < other_node_id) {
            return NodeRelation::Before;
        }
        if (m_node_id == other_node_id) {
            return NodeRelation::Equal;
        }
        if (m_node_id > other_node_id) {
            return NodeRelation::After;
        }
        assert(false);
    }

    bool operator<(InterestingNode const& other) const {
        return m_node_id > other.m_node_id;
    }

private:
    osmium::object_id_type m_node_id;
    osmium::object_id_type m_way_id;
};

class FirstLocationExtractor : public osmium::handler::Handler {
public:
    FirstLocationExtractor(std::vector<WayEntry> const& way_entries) {
        for (auto const& way_entry : way_entries) {
            for (osmium::object_id_type node_id : way_entry.nodes()) {
                m_interesting_nodes.emplace_back(node_id, way_entry.way_id());
            }
        }
        // Note that this ends up sorting in order DESCENDING by node ID:
        std::sort(m_interesting_nodes.begin(), m_interesting_nodes.end());
        assert(m_interesting_nodes.size() >= 2);
        assert(m_interesting_nodes[0].node_id() >= m_interesting_nodes[1].node_id());
    }

    void node(const osmium::Node& node) {
        while (!m_interesting_nodes.empty()) {
            switch (m_interesting_nodes.back().relative_to(node.id())) {
            case NodeRelation::Before:
                // The current interesting node is not in the dataset. Remove it,
                // and try the next InterestingNode.
                m_interesting_nodes.pop_back();
                break; // … from the switch block, not the loop.
            case NodeRelation::Equal: {
                // The current interesting node is encountered in the dataset.
                // Maybe emit it, go to the next InterestingNode, and make sure we
                // don't emit anything again for this Way.
                osmium::object_id_type way = m_interesting_nodes.back().way_id();
                if (m_emitted_ways.count(way) == 0) {
                    m_emitted_ways.emplace(way);
                    auto loc = node.location();
                    printf("w%lu x%d y%d\n", way, loc.x(), loc.y());
                }
                m_interesting_nodes.pop_back();
                return;
            }
            case NodeRelation::After:
                // The encountered node is definitely not interesting.
                return;
            }
        }
    }

private:
    std::vector<InterestingNode> m_interesting_nodes;
    std::unordered_set<osmium::object_id_type> m_emitted_ways;
};

int main() {
    WayNodesExtractor way_nodes;
    printf("# First pass for ways on %s …\n", INPUT_FILENAME);
    {
        osmium::io::Reader reader{INPUT_FILENAME, osmium::osm_entity_bits::way};
        osmium::apply(reader, way_nodes);
        reader.close();
    }
    printf("# Sorting …\n");
    FirstLocationExtractor first_locs {way_nodes.way_entries()};
    way_nodes.clear();
    printf("# Second pass for nodes on %s …\n", INPUT_FILENAME);
    {
        osmium::io::Reader reader{INPUT_FILENAME, osmium::osm_entity_bits::node};
        osmium::apply(reader, first_locs);
        reader.close();
    }
    printf("# Done iterating.\n");
    return 0;
}

// $ OSMIUM_CLEAN_PAGE_CACHE_AFTER_READ=no hyperfine ./extract_some_ways_linear_scan
// Benchmark 1: ./extract_some_ways_linear_scan
//   Time (mean ± σ):      9.741 s ±  0.034 s    [User: 136.355 s, System: 1.748 s]
//   Range (min … max):    9.685 s …  9.782 s    10 runs

// $ cachedel /scratch/osm/planet-231002.osm.pbf && /usr/bin/time ./extract_some_ways_linear_scan > ways_ls.lst
// 2558.77user 86.89system 3:47.55elapsed 1162%CPU (0avgtext+0avgdata 797736maxresident)k
// 298078264inputs+32outputs (0major+11986905minor)pagefaults 0swaps
